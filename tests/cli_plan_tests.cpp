#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <atomic>

#include "kubeforward/cli.h"
#include "kubeforward/runtime/process_runner.h"
#include "kubeforward/runtime/state_store.h"

#ifndef KF_SOURCE_DIR
#error "KF_SOURCE_DIR must be defined"
#endif

namespace {

std::string Fixture(const std::string& name) {
  return std::string(KF_SOURCE_DIR) + "/tests/fixtures/" + name;
}

std::filesystem::path FixtureDir() { return std::string(KF_SOURCE_DIR) + "/tests/fixtures"; }

std::filesystem::path TempDir() {
  const auto base = std::filesystem::temp_directory_path() / "kubeforward-cli-tests";
  std::filesystem::create_directories(base);
  return base;
}

std::string UniqueSuffix() {
  static std::atomic<unsigned long> counter{0};
  std::ostringstream oss;
  oss << ::getpid() << "-" << counter.fetch_add(1, std::memory_order_relaxed);
  return oss.str();
}

struct CliResult {
  int exit_code = 0;
  std::string out;
  std::string err;
};

class ScopedStreamCapture {
 public:
  ScopedStreamCapture() : original_out_(std::cout.rdbuf(out_.rdbuf())), original_err_(std::cerr.rdbuf(err_.rdbuf())) {}

  ~ScopedStreamCapture() {
    std::cout.rdbuf(original_out_);
    std::cerr.rdbuf(original_err_);
  }

  std::string out() const { return out_.str(); }
  std::string err() const { return err_.str(); }

 private:
  std::ostringstream out_;
  std::ostringstream err_;
  std::streambuf* original_out_ = nullptr;
  std::streambuf* original_err_ = nullptr;
};

class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const std::filesystem::path& new_path) : original_(std::filesystem::current_path()) {
    std::filesystem::current_path(new_path);
  }

  ~ScopedCurrentPath() { std::filesystem::current_path(original_); }

 private:
  std::filesystem::path original_;
};

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* key, const char* value) : key_(key) {
    const char* existing = std::getenv(key_);
    if (existing != nullptr) {
      had_original_ = true;
      original_ = existing;
    }
    ::setenv(key_, value, 1);
  }

  ~ScopedEnvVar() {
    if (had_original_) {
      ::setenv(key_, original_.c_str(), 1);
      return;
    }
    ::unsetenv(key_);
  }

 private:
  const char* key_;
  bool had_original_ = false;
  std::string original_;
};

class ScopedStateFile {
 public:
  ScopedStateFile() : path_(TempDir() / ("state-" + UniqueSuffix() + ".yaml")),
                      env_("KUBEFORWARD_STATE_FILE", path_.string().c_str()) {
    std::filesystem::remove(path_);
  }

  ~ScopedStateFile() { std::filesystem::remove(path_); }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  ScopedEnvVar env_;
};

class ScopedCleanup {
 public:
  explicit ScopedCleanup(std::function<void()> callback) : callback_(std::move(callback)) {}

  ~ScopedCleanup() {
    if (active_ && callback_) {
      callback_();
    }
  }

  void Dismiss() { active_ = false; }

 private:
  bool active_ = true;
  std::function<void()> callback_;
};

class ScopedListeningSocket {
 public:
  ScopedListeningSocket(const std::string& bind_address, int port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
      return;
    }

    const int reuse = 1;
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
      ::close(fd_);
      fd_ = -1;
      return;
    }
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
      ::close(fd_);
      fd_ = -1;
      return;
    }
    if (::listen(fd_, 1) != 0) {
      ::close(fd_);
      fd_ = -1;
      return;
    }

    sockaddr_in bound_addr{};
    socklen_t bound_addr_len = sizeof(bound_addr);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&bound_addr), &bound_addr_len) != 0) {
      ::close(fd_);
      fd_ = -1;
      return;
    }

    port_ = ntohs(bound_addr.sin_port);
  }

  ~ScopedListeningSocket() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  bool ok() const { return fd_ >= 0; }
  int port() const { return port_; }

 private:
  int fd_ = -1;
  int port_ = 0;
};

std::filesystem::path TempConfigCopyPath(const std::string& stem) {
  return TempDir() / (stem + "-" + UniqueSuffix() + ".yaml");
}

std::filesystem::path TempPath(const std::string& stem, const std::string& extension) {
  return TempDir() / (stem + "-" + UniqueSuffix() + extension);
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream output(path, std::ios::trunc);
  output << contents;
}

std::filesystem::path WriteExecutableScript(const std::string& stem, const std::string& body) {
  const auto path = TempPath(stem, ".sh");
  WriteFile(path, body);
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec |
          std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
          std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
      std::filesystem::perm_options::replace);
  return path;
}

std::filesystem::path WriteConfigFile(const std::string& stem, const std::string& body) {
  const auto path = TempPath(stem, ".yaml");
  WriteFile(path, body);
  return path;
}

std::string SingleForwardConfigContents(const std::string& env_name, int local_port,
                                        const std::string& bind_address = "127.0.0.1") {
  return std::string("version: 1\n"
                     "metadata:\n"
                     "  project: cli-test\n"
                     "defaults:\n"
                     "  namespace: default\n"
                     "  bindAddress: ") +
         bind_address +
         "\n"
         "environments:\n"
         "  " +
         env_name +
         ":\n"
         "    forwards:\n"
         "      - name: api\n"
         "        resource:\n"
         "          kind: deployment\n"
         "          name: api\n"
         "        ports:\n"
         "          - local: " +
         std::to_string(local_port) +
         "\n"
         "            remote: 80\n";
}

std::filesystem::path WriteSingleForwardConfig(const std::string& stem, const std::string& env_name, int local_port,
                                               const std::string& bind_address = "127.0.0.1") {
  return WriteConfigFile(stem, SingleForwardConfigContents(env_name, local_port, bind_address));
}

std::string TwoForwardConfigContents(const std::string& env_name, int first_local_port, int second_local_port,
                                     const std::string& bind_address = "127.0.0.1") {
  return std::string("version: 1\n"
                     "metadata:\n"
                     "  project: cli-test\n"
                     "defaults:\n"
                     "  namespace: default\n"
                     "  bindAddress: ") +
         bind_address +
         "\n"
         "environments:\n"
         "  " +
         env_name +
         ":\n"
         "    forwards:\n"
         "      - name: api-a\n"
         "        resource:\n"
         "          kind: deployment\n"
         "          name: api-a\n"
         "        ports:\n"
         "          - local: " +
         std::to_string(first_local_port) +
         "\n"
         "            remote: 80\n"
         "      - name: api-b\n"
         "        resource:\n"
         "          kind: deployment\n"
         "          name: api-b\n"
         "        ports:\n"
         "          - local: " +
         std::to_string(second_local_port) +
         "\n"
         "            remote: 80\n";
}

std::filesystem::path WriteTwoForwardConfig(const std::string& stem, const std::string& env_name, int first_local_port,
                                            int second_local_port, const std::string& bind_address = "127.0.0.1") {
  return WriteConfigFile(stem, TwoForwardConfigContents(env_name, first_local_port, second_local_port, bind_address));
}

bool IsPidAlive(int pid) {
  if (pid <= 0) {
    return false;
  }
  if (::kill(pid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
}

int RandomLocalPort() { return 20000 + (std::rand() % 10000); }

int ReserveTcpPort(const std::string& bind_address) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return 0;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return 0;
  }
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return 0;
  }

  socklen_t addr_len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
    ::close(fd);
    return 0;
  }

  const int port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

int FindAvailableLoopbackPort() {
  for (int attempt = 0; attempt < 32; ++attempt) {
    const int port = ReserveTcpPort("127.0.0.1");
    if (port > 0) {
      return port;
    }
  }

  return RandomLocalPort();
}

bool CanConnectTcpPort(int port, const std::string& bind_address = "127.0.0.1") {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return false;
  }

  const bool ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
  ::close(fd);
  return ok;
}

bool WaitForPortClosed(int port, int timeout_ms = 6000) {
  for (int waited_ms = 0; waited_ms < timeout_ms; waited_ms += 50) {
    if (!CanConnectTcpPort(port)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return !CanConnectTcpPort(port);
}

CliResult RunAndCapture(const std::vector<std::string>& args) {
  ScopedStreamCapture capture;
  const int exit_code = kubeforward::run_cli(args);
  return {.exit_code = exit_code, .out = capture.out(), .err = capture.err()};
}

}  // namespace

TEST_CASE("plan succeeds with valid config", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "-f", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("default command is plan when no subcommand is provided", "[cli]") {
  ScopedCurrentPath cwd(FixtureDir());

  std::vector<std::string> args = {"kubeforward"};
  const int exit_code = kubeforward::run_cli(args);

  REQUIRE(exit_code == 0);
}

TEST_CASE("empty argv is handled without crashing", "[cli]") {
  std::vector<std::string> args = {};
  REQUIRE(kubeforward::run_cli(args) == 1);
}

TEST_CASE("default plan command accepts plan flags without subcommand", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--file", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("default plan command accepts equals syntax for long flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--file=" + Fixture("basic.yaml"), "--env=dev", "--verbose=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") == std::string::npos);
}

TEST_CASE("default plan command rejects equals syntax for short flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "-f=" + Fixture("basic.yaml"), "-e=dev", "-v=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("Option '=' does not exist") != std::string::npos);
}

TEST_CASE("plan command rejects equals syntax for short flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "-f=" + Fixture("basic.yaml"), "-e=dev", "-v=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("Option '=' does not exist") != std::string::npos);
}

TEST_CASE("version flag prints app version", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--version"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out == std::string(KF_APP_VERSION) + "\n");
}

TEST_CASE("up defaults to the first environment when --env is omitted", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("up: starting forwards") != std::string::npos);
  CHECK(result.out.find("env: dev") != std::string::npos);
}

TEST_CASE("up supports daemon mode and explicit environment", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev", "--daemon"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("env: dev") != std::string::npos);
  CHECK(result.out.find("mode: daemon") != std::string::npos);
}

TEST_CASE("up supports verbose output", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev", "--verbose"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("forward names:") != std::string::npos);
  CHECK(result.out.find("- api") != std::string::npos);
}

TEST_CASE("down defaults to all environments when --env is omitted", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;

  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev"}) == 0);
  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "prod"}) == 0);

  const auto result = RunAndCapture({"kubeforward", "down", "--file", Fixture("basic.yaml")});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("scope: all environments") != std::string::npos);
  CHECK(result.out.find("environments: 2") != std::string::npos);
}

TEST_CASE("down supports explicit environment and daemon mode", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;

  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev"}) == 0);

  const auto result = RunAndCapture({"kubeforward", "down", "--file", Fixture("basic.yaml"), "-e", "dev", "-d"});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("scope: environment") != std::string::npos);
  CHECK(result.out.find("env: dev") != std::string::npos);
  CHECK(result.out.find("mode: daemon") != std::string::npos);
}

TEST_CASE("down supports verbose output", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;

  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev"}) == 0);
  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "prod"}) == 0);

  const auto result = RunAndCapture({"kubeforward", "down", "--file", Fixture("basic.yaml"), "--verbose"});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("environment breakdown:") != std::string::npos);
  CHECK(result.out.find("environments: 2") != std::string::npos);
  CHECK(result.out.find("stopped: 2") != std::string::npos);
}

TEST_CASE("down stops tracked sessions when config is missing", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  ScopedStateFile state_file;

  const auto config_copy = TempConfigCopyPath("down-missing-config");
  std::filesystem::copy_file(Fixture("basic.yaml"), config_copy, std::filesystem::copy_options::overwrite_existing);

  REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", config_copy.string(), "--env", "dev"}) == 0);
  std::filesystem::remove(config_copy);

  const auto result = RunAndCapture({"kubeforward", "down", "--file", config_copy.string(), "--env", "dev", "--verbose"});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("scope: environment") != std::string::npos);
  CHECK(result.out.find("env: dev") != std::string::npos);
  CHECK(result.out.find("stopped: 1") != std::string::npos);
}

TEST_CASE("up preserves the running session when replacement kubectl is invalid", "[cli]") {
  ScopedStateFile state_file;
  const auto kubectl_script = WriteExecutableScript("fake-kubectl", "#!/bin/sh\ntrap 'exit 0' TERM INT\nsleep 30\n");
  const auto config_copy = WriteSingleForwardConfig("replacement-invalid-kubectl", "dev", FindAvailableLoopbackPort());
  ScopedCleanup cleanup([&]() {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    (void)kubeforward::run_cli({"kubeforward", "down", "--file", config_copy.string(), "--env", "dev"});
  });

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", config_copy.string(), "--env", "dev", "--daemon"}) ==
            0);
  }

  const auto initial_state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(initial_state.ok());
  REQUIRE(initial_state.state.sessions.size() == 1);
  REQUIRE(initial_state.state.sessions.at(0).forwards.size() == 1);
  const int original_pid = initial_state.state.sessions.at(0).forwards.at(0).pid;
  REQUIRE(IsPidAlive(original_pid));

  {
    ScopedEnvVar invalid_kubectl("KUBEFORWARD_KUBECTL_BIN", "/path/that/does/not/exist");
    const auto result = RunAndCapture({"kubeforward", "up", "--file", config_copy.string(), "--env", "dev"});
    REQUIRE(result.exit_code == 2);
    CHECK(result.err.find("kubectl executable is not available") != std::string::npos);
  }

  const auto after_failure_state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(after_failure_state.ok());
  REQUIRE(after_failure_state.state.sessions.size() == 1);
  REQUIRE(after_failure_state.state.sessions.at(0).forwards.size() == 1);
  CHECK(after_failure_state.state.sessions.at(0).forwards.at(0).pid == original_pid);
  CHECK(IsPidAlive(original_pid));

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    REQUIRE(kubeforward::run_cli({"kubeforward", "down", "--file", config_copy.string(), "--env", "dev"}) == 0);
  }
  cleanup.Dismiss();
}

TEST_CASE("up restores the original session when replacement preflight fails after stop", "[cli]") {
  ScopedStateFile state_file;
  const auto kubectl_script = WriteExecutableScript("fake-kubectl", "#!/bin/sh\ntrap 'exit 0' TERM INT\nsleep 30\n");
  const int original_port = FindAvailableLoopbackPort();
  const auto config_path = WriteSingleForwardConfig("replacement-preflight-failure", "dev", original_port);
  ScopedCleanup cleanup([&]() {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    (void)kubeforward::run_cli({"kubeforward", "down", "--file", config_path.string(), "--env", "dev"});
  });

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    REQUIRE(kubeforward::run_cli({"kubeforward", "up", "--file", config_path.string(), "--env", "dev", "--daemon"}) ==
            0);
  }

  const auto initial_state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(initial_state.ok());
  REQUIRE(initial_state.state.sessions.size() == 1);
  const int original_pid = initial_state.state.sessions.at(0).forwards.at(0).pid;

  ScopedListeningSocket blocker("127.0.0.1", 0);
  REQUIRE(blocker.ok());
  REQUIRE(blocker.port() != original_port);
  const int blocked_port = blocker.port();
  WriteFile(config_path, SingleForwardConfigContents("dev", blocked_port));

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    const auto result = RunAndCapture({"kubeforward", "up", "--file", config_path.string(), "--env", "dev"});
    REQUIRE(result.exit_code == 2);
    CHECK(result.err.find("previous session was restored") != std::string::npos);
  }

  const auto restored_state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(restored_state.ok());
  REQUIRE(restored_state.state.sessions.size() == 1);
  REQUIRE(restored_state.state.sessions.at(0).forwards.size() == 1);
  CHECK(restored_state.state.sessions.at(0).forwards.at(0).local_port == original_port);
  CHECK(restored_state.state.sessions.at(0).forwards.at(0).pid != original_pid);
  CHECK(IsPidAlive(restored_state.state.sessions.at(0).forwards.at(0).pid));

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    REQUIRE(kubeforward::run_cli({"kubeforward", "down", "--file", config_path.string(), "--env", "dev"}) == 0);
  }
  cleanup.Dismiss();
}

TEST_CASE("up stops started forwards when state persistence fails", "[cli]") {
  const auto state_dir = TempPath("state-save-dir", "");
  std::filesystem::create_directories(state_dir);
  const auto state_path = state_dir / "state.yaml";
  const auto state_lock_path = state_dir / "state.yaml.lock";
  WriteFile(state_path, "sessions: []\n");
  WriteFile(state_lock_path, "");
  std::filesystem::permissions(
      state_dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
      std::filesystem::perm_options::replace);
  ScopedEnvVar state_file("KUBEFORWARD_STATE_FILE", state_path.string().c_str());

  const int local_port = FindAvailableLoopbackPort();
  const auto kubectl_script = WriteExecutableScript(
      "fake-kubectl-binder",
      "#!/bin/sh\n"
      "local_port=\"${3%%:*}\"\n"
      "exec /usr/bin/python3 -c 'import signal, socket, sys, time; "
      "s = socket.socket(); "
      "s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); "
      "s.bind((\"127.0.0.1\", int(sys.argv[1]))); "
      "s.listen(1); "
      "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
      "signal.signal(signal.SIGINT, signal.SIG_IGN); "
      "time.sleep(30)' \"$local_port\"\n");

  ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
  const auto config_path = WriteSingleForwardConfig("state-save-failure", "dev", local_port);
  const auto result = RunAndCapture({"kubeforward", "up", "--file", config_path.string(), "--env", "dev"});
  REQUIRE(result.exit_code == 2);
  CHECK(result.err.find("failed to save runtime state") != std::string::npos);
  CHECK(WaitForPortClosed(local_port, 6000));
  std::filesystem::permissions(
      state_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}

TEST_CASE("up keeps foreground sessions attached until the child exits", "[cli]") {
  ScopedStateFile state_file;
  const auto kubectl_script = WriteExecutableScript("fake-kubectl-foreground", "#!/bin/sh\nsleep 1\n");
  const auto config_path = WriteSingleForwardConfig("foreground-up", "dev", FindAvailableLoopbackPort());

  ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
  const auto start = std::chrono::steady_clock::now();
  const int exit_code = kubeforward::run_cli({"kubeforward", "up", "--file", config_path.string(), "--env", "dev"});
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

  REQUIRE(exit_code == 0);
  CHECK(elapsed_ms >= 800);

  const auto state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(state.ok());
  CHECK(state.state.sessions.empty());
}

TEST_CASE("up fails foreground sessions when one forward exits before the others", "[cli]") {
  ScopedStateFile state_file;
  const auto kubectl_script = WriteExecutableScript(
      "fake-kubectl-foreground-multi",
      "#!/bin/sh\n"
      "local_port=\"${3%%:*}\"\n"
      "if [ \"$local_port\" = \"$KUBEFORWARD_SHORT_PORT\" ]; then\n"
      "  sleep 1\n"
      "else\n"
      "  trap 'exit 0' TERM INT\n"
      "  sleep 30\n"
      "fi\n");
  const int first_port = FindAvailableLoopbackPort();
  int second_port = FindAvailableLoopbackPort();
  while (second_port == first_port) {
    second_port = FindAvailableLoopbackPort();
  }
  const auto config_path = WriteTwoForwardConfig("foreground-up-multi", "dev", first_port, second_port);

  ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
  ScopedEnvVar short_port("KUBEFORWARD_SHORT_PORT", std::to_string(first_port).c_str());
  const auto result = RunAndCapture({"kubeforward", "up", "--file", config_path.string(), "--env", "dev"});

  REQUIRE(result.exit_code == 2);

  const auto state = kubeforward::runtime::LoadState(state_file.path());
  REQUIRE(state.ok());
  CHECK(state.state.sessions.empty());
}

TEST_CASE("up refuses to replace sessions that cannot be rolled back safely", "[cli]") {
  ScopedStateFile state_file;
  const auto kubectl_script = WriteExecutableScript("fake-kubectl-upgrade", "#!/bin/sh\ntrap 'exit 0' TERM INT\nsleep 30\n");
  const auto config_path = WriteSingleForwardConfig("upgrade-session", "dev", FindAvailableLoopbackPort());

  kubeforward::runtime::PosixProcessRunner runner;
  kubeforward::runtime::StartProcessRequest request;
  request.argv = {kubectl_script.string(), "port-forward", "deployment/api", "7000:80"};
  request.cwd = std::filesystem::current_path();

  std::string error;
  const auto started = runner.Start(request, error);
  REQUIRE(started.has_value());
  REQUIRE(error.empty());

  kubeforward::runtime::RuntimeState state;
  kubeforward::runtime::ManagedSession session;
  session.id = "legacy-session";
  session.config_path = std::filesystem::absolute(config_path).string();
  session.environment = "dev";
  session.daemon = true;
  session.started_at_utc = "2026-02-28T00:00:00Z";
  session.forwards.push_back(kubeforward::runtime::ManagedForwardProcess{
      .environment = "dev",
      .forward_name = "api",
      .local_port = 7000,
      .remote_port = 80,
      .pid = started->pid,
  });
  state.sessions.push_back(session);
  REQUIRE(kubeforward::runtime::SaveState(state_file.path(), state, error));
  REQUIRE(error.empty());

  {
    ScopedEnvVar kubectl_bin("KUBEFORWARD_KUBECTL_BIN", kubectl_script.string().c_str());
    const auto result = RunAndCapture({"kubeforward", "up", "--file", config_path.string(), "--env", "dev"});
    REQUIRE(result.exit_code == 2);
    CHECK(result.err.find("cannot be replaced safely") != std::string::npos);
  }

  CHECK(IsPidAlive(started->pid));
  CHECK(runner.Stop(started->pid, error));
}

TEST_CASE("commands are mutually exclusive by subcommand position", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "up", "plan"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code != 0);
  CHECK(result.err.find("up:") != std::string::npos);
}

TEST_CASE("unknown top-level flag is routed to plan and rejected", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--unknown"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("plan: ") != std::string::npos);
  CHECK(result.err.find("does not exist") != std::string::npos);
}

TEST_CASE("unknown top-level equals flag is routed to plan and rejected", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--version=1"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("plan: ") != std::string::npos);
  CHECK(result.err.find("does not exist") != std::string::npos);
}

TEST_CASE("plan fails when config missing", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("missing.yaml")};
  REQUIRE(kubeforward::run_cli(args) != 0);
}

TEST_CASE("plan defaults to kubeforward.yaml in current directory", "[cli]") {
  ScopedCurrentPath cwd(FixtureDir());

  std::vector<std::string> args = {"kubeforward", "plan", "--env", "dev"};
  const int exit_code = kubeforward::run_cli(args);

  REQUIRE(exit_code == 0);
}

TEST_CASE("plan without env shows all environments", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") != std::string::npos);
}

TEST_CASE("plan verbose shows detailed fields", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml"), "--verbose"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Config file: ") != std::string::npos);
  CHECK(result.out.find("Metadata:") != std::string::npos);
  CHECK(result.out.find("Defaults:") != std::string::npos);
  CHECK(result.out.find("settings:") != std::string::npos);
  CHECK(result.out.find("ports:") != std::string::npos);
}

TEST_CASE("plan renders inherited forwards from the resolved environment graph", "[cli]") {
  const auto result =
      RunAndCapture({"kubeforward", "plan", "--file", Fixture("extends_with_defaults.yaml"), "--env", "child"});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: child") != std::string::npos);
  CHECK(result.out.find("base-api") != std::string::npos);
  CHECK(result.out.find("Forwards (1)") != std::string::npos);
}

TEST_CASE("plan verbose renders resolved child settings", "[cli]") {
  const auto result = RunAndCapture(
      {"kubeforward", "plan", "--file", Fixture("extends_with_child_overrides.yaml"), "--env", "child", "--verbose"});

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("namespace: child-ns") != std::string::npos);
  CHECK(result.out.find("bindAddress: 127.0.0.2") != std::string::npos);
  CHECK(result.out.find("base-api") != std::string::npos);
}

TEST_CASE("plan env verbose filters to selected environment", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml"), "-e", "dev", "-v"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") == std::string::npos);
}

TEST_CASE("default plan with verbose shows all environments", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "-v", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") != std::string::npos);
}

TEST_CASE("plan accepts json config through --file", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.json"), "--env", "dev"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
}
