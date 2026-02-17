#include <iostream>
#include <string>

#include <cxxopts.hpp>

int main(int argc, char* argv[]) {
  bool show_help = false;
  bool flag1 = false;
  std::string flag2;

  cxxopts::Options options("kubeforward", "Kubeforward CLI");
  options.add_options()("h", "Show help message", cxxopts::value<bool>(show_help)->default_value("false"))(
      "flag1", "Enable flag1 (boolean)", cxxopts::value<bool>(flag1)->default_value("false")->implicit_value("true"))(
      "flag2", "Set flag2 value", cxxopts::value<std::string>(flag2));

  try {
    options.parse_positional({});
    options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  if (show_help) {
    std::cout << options.help() << "\n";
  }

  std::cout << "Flag summary:\n"
            << "  help: " << (show_help ? "true" : "false") << "\n"
            << "  flag1: " << (flag1 ? "true" : "false") << "\n"
            << "  flag2: " << (flag2.empty() ? "<unset>" : flag2) << "\n";

  return 0;
}
