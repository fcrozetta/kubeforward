# VS Code Setup (C++ + vcpkg)

## Required extensions
1. **C/C++** (ms-vscode.cpptools) – provides IntelliSense for headers pulled via vcpkg.
2. **CMake Tools** (ms-vscode.cmake-tools) – drives configure/build tasks.
3. **CodeLLDB** (vadimcn.vscode-lldb) – debug adapter that works on modern macOS (lldb-mi is deprecated).

## Environment prerequisites
- Install vcpkg and set `VCPKG_ROOT` globally (e.g., in `~/.zprofile`). Kubeforward assumes `/Users/fcrozetta/projects/vcpkg`; adjust `.vscode/settings.json` + `launch.json` if yours differs.
- Ensure CMake and a C++20 toolchain are on PATH (`brew install cmake llvm` or Xcode CLT).

## Workspace configuration
The repo ships `.vscode/settings.json` with:
- `cmake.buildDirectory`: `${workspaceFolder}/build`.
- `cmake.configureArgs`: adds `-DCMAKE_TOOLCHAIN_FILE=${env:VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` so every configure run uses manifest mode.
- `cmake.environment`: provides `VCPKG_ROOT` for CMake Tools if your shell doesn’t export it.
- `cmake.configureOnOpen`: auto-configures when the folder opens.

If your `VCPKG_ROOT` differs, update `.vscode/settings.json` and `.vscode/launch.json` accordingly.

## Configure & build
1. Open the folder in VS Code.
2. Let CMake Tools select the default kit (AppleClang). If prompted, run “CMake: Select Kit”.
3. Run “CMake: Configure” once; it will install cxxopts/yaml-cpp via manifest mode.
4. Build via the CMake status bar button or the `CMake: build` task (mapped in `.vscode/tasks.json`).

## Debugging
1. Install CodeLLDB extension.
2. Select the “kubeforward (CodeLLDB)” configuration from the Run and Debug panel.
3. Hit F5. PreLaunch task builds the target; the adapter launches `build/kubeforward` with `VCPKG_ROOT` in the environment.
4. Inline breakpoints work because we set `settings set target.inline-breakpoint-strategy always` during init.

## Troubleshooting
- **Toolchain not found**: confirm `VCPKG_ROOT` is exported or update `.vscode/settings.json`.
- **Missing extensions**: VS Code will prompt; install the ones above.
- **Launch failures**: ensure `build/kubeforward` exists (run CMake build) and CodeLLDB is enabled. If you prefer cpptools, you must install a standalone `lldb-mi` and point `miDebuggerPath` there.

