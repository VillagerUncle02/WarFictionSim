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
