# CI 反馈记录 feature/setup-foundation

## 第 1 轮（run #31237741962，2026-08-08）
- 结论：failure（步骤：Configure CMake）
- 失败原因：vcpkg 安装使用 `--depth 1` 浅克隆，builtin-baseline 提交
  `c4d9956c0c10a4742840a5e7d93efa2e0015c865` 不在本地，`git show versions/baseline.json` 失败。
- 修复：改为 `--filter=blob:none --no-checkout` 部分克隆并检出 baseline 提交（ci/nightly/release 三处同步）。
- 修复提交：随本记录提交（见 git log）。

## 第 2 轮（run #31237809654，2026-08-08）
- 结论：failure（步骤：Configure CMake，同一步骤）
- 失败原因：部分克隆（blob:none）的按需取 blob 在 runner 上不可靠，`git show <baseline>:versions/baseline.json`
  仍报 "exists on disk, but not in <commit>"；API 已验证 baseline 提交确实包含该文件（224KB）。
- 修复：改为完整克隆 + 检出 baseline，并补齐 clone/checkout/bootstrap 的退出码检查。

## 第 3 轮（run #31237911669，2026-08-08）
- 结论：failure（步骤：Configure CMake，同一步骤）
- 失败原因（真正根因）：runner 镜像**预装 C:/vcpkg**（旧提交），`Test-Path` 判断使克隆被跳过，
  preinstalled vcpkg 不含 baseline 提交，`git show` 失败（job 全程仅 24s 佐证克隆未发生）。
- 修复：预装存在时强制 `git fetch --depth 1 origin <baseline>` + `checkout --force` 对齐 baseline；
  不存在时才全新克隆；vcpkg.exe 缺失时才 bootstrap。

## 第 4 轮（run #31237992886，2026-08-08）
- 结论：failure（步骤：Build C# UI & tools）——vcpkg 修复生效（CMake/C++/CTest 已通过）
- 失败原因：UnitTest1.cs 缺少 `using Xunit;`，`[Fact]` 无法解析（CS0246）。
- 修复：补充 `using Xunit;`。
