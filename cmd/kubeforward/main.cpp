#include "kubeforward/cli.h"

#include <vector>
#include <string>

int main(int argc, char *argv[])
{
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i)
  {
    args.emplace_back(argv[i]);
  }
  return kubeforward::run_cli(args);
}
