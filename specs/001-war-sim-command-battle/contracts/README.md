# Contracts: 战争幻想模拟器 —— 运行时契约（Phase 1 输出）

本目录定义模拟核心对外的稳定契约，供 C++ 核心、C# 表现层、无头 CLI 与工具共用：

- [command-schema.md](command-schema.md)：命令 JSON Schema 与校验管道
- [sim-c-api.md](sim-c-api.md)：C ABI（P/Invoke 边界）
- [save-format.md](save-format.md)：存档格式与迁移

契约版本由 `wfs_sim_version`（C ABI）与存档 `format_version` 分别管理；运行时契约变更必须同步更新 `contracts/schemas/` 下的 JSON Schema 文件（宪法第 18 条可复现构建与契约可测）。
