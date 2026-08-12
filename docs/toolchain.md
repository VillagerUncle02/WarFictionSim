# WarFictionSim 工具链（2026-08 定案）

本文档记录 B 方案重组后的工具链分工与命令，是 README「本地构建」的补充说明。

## 分工

| 侧 | 技术栈 | 入口 | 产物 |
|---|---|---|---|
| C/C++（`native/`） | CMake 3.31 + Ninja + clang-cl + vcpkg | `native/CMakeLists.txt`、`CMakePresets.json` | `sim_core.dll`（C ABI）、`sim_headless.exe`、CTest |
| C#（`app/`） | .NET 10 SDK（dotnet / MSBuild） | `app/WarFictionSim.sln` | WPF 游戏界面、工具、xUnit 测试 |
| 契约 | — | `contracts/`（JSON Schema、C ABI 头） | 两侧唯一共享依赖 |

## 为什么这样选

- **CMake 3.31.x，不要 4.x**：CMake 4.x 对无版本 `find_package` 也会校验旧式
  `ConfigVersion.cmake`，本项目 vcpkg 的 `json-schema-validator 2.4.0` 会因此
  配置失败（已实测）。3.31.x 无此问题；当前 PATH 与 VS 自带均为 3.31。
- **clang-cl 而不是 clang**：保持 MSVC ABI，vcpkg 继续用 `x64-windows` 包，
  C# P/Invoke 不受影响；构建速度来自 Ninja 并行。
- **CodeLLDB 需要 DWARF**：clang-cl 默认生成 CodeView/PDB，LLDB 无法读取。
  `native/CMakeLists.txt` 在 Debug 配置加了 `/clang:-gdwarf` 并统一用
  `lld-link` 链接（`-fuse-ld=lld`），因此 Debug 构建可用 CodeLLDB 断点/单步。
- **clangd 直接读 Ninja 编译数据库**：`CMAKE_EXPORT_COMPILE_COMMANDS=ON` 由
  preset 固定，`build/clang-cl-debug/compile_commands.json` 即 clangd 数据源；
  `.clangd` 已指向该目录（默认 Debug）。
- **C# 不进 CMake**：CMake 的 C# 支持仅限 VS 生成器且面向旧式工程，管不了
  SDK 风格 WPF 工程。C# 由 dotnet 构建，与原生侧通过 `sim_core.dll` 对接。

## 常用命令

```powershell
$env:VCPKG_ROOT = 'C:\Users\ASUS\vcpkg'   # 本机 vcpkg 位置；CI 固定为 C:/vcpkg

cd native
cmake --preset clang-cl-debug             # 配置（自动安装 manifest 依赖）
cmake --build --preset clang-cl-debug     # 构建
ctest --preset clang-cl-debug             # 测试
```

Release 把 `debug` 换成 `release`。C# 侧：

```powershell
dotnet build app/WarFictionSim.sln -c Debug
dotnet test  app/WarFictionSim.sln -c Debug --no-build
```

`app/Directory.Build.targets` 会在 C# 构建后自动把
`native/build/clang-cl-<config>/**/*.dll` 复制到 C# 输出目录；找不到时跳过
（骨架期无 DLL 是正常的）。

## CI（.github/workflows/ci.yml）

- `native-build`：windows-2022 固定镜像；用 VS 自带的 CMake 3.31 + Ninja +
  clang-cl 构建 Release、跑 CTest、无头确定性/存档回归、clang-format 与
  clang-tidy（直接使用编译数据库），上传 DLL/exe 产物。
- `app-build`：依赖 native 产物，下载后 `dotnet build/test/format`。
- `check-pr-order`：链式 PR 队列检查（保持不变）。

## 已知注意事项

- 本地 `VCPKG_ROOT` 必须指向 vcpkg；未设置时 CI 会固定为 `C:/vcpkg`。
- 本地构建用 `VCPKG_MANIFEST_MODE=ON`（与 CI 一致）；如本地 vcpkg 仓库不在
  `builtin-baseline` 提交上，首次配置会尝试 `git fetch` 该提交。
- VS Code 调试配置见 `.vscode/launch.json`：LLDB 调试 `sim_headless` /
  `sim_tests`（Debug + DWARF），coreclr 调试 C#。
- 不要把 `native/` 或 `app/` 重新移回根目录：specs/tasks 中的历史路径是
  旧布局引用，新工作以本文档与 CI 为准。
