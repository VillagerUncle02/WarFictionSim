# contracts/schemas/ —— 运行时 JSON Schema

存放 v1 实际使用的 JSON Schema（命令、AI 决策输出、剧本、存档头等），
与 `specs/001-war-sim-command-battle/contracts/` 的设计契约保持同步（CHK126）。

## 约定

- 所有 Schema 携带 `schema_version`；破坏性变更递增版本并提供迁移（宪法第 13 条）；
- 契约是唯一事实来源：模拟核心、AI 后端、表现层与工具均按契约实现与测试（SC-010）。

具体 Schema 文件由对应实现任务生成（如 T014 命令 Schema、T013 数据加载校验）。
