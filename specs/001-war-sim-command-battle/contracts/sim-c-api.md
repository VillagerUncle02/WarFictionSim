# C ABI 契约（P/Invoke 边界）

`sim` 核心对外暴露纯 C ABI，C# 通过 P/Invoke 消费；不使用 C++/CLI（宪法第 14 条：跨层只传纯数据）。

## 概览

```c
typedef struct wfs_sim_handle wfs_sim_handle;

wfs_sim_handle*   wfs_sim_create(const char* scenario_path, uint64_t seed, int threads);
void              wfs_sim_destroy(wfs_sim_handle* h);
const char*       wfs_sim_version(void);

wfs_sim_result    wfs_sim_step(wfs_sim_handle* h);
wfs_sim_result    wfs_sim_inject_command(wfs_sim_handle* h, const char* command_json);
wfs_sim_result    wfs_sim_inject_ai_decision(wfs_sim_handle* h, const char* decision_json);
wfs_sim_result    wfs_sim_get_snapshot(wfs_sim_handle* h, char* out_buf, size_t buf_size, size_t* out_len);
wfs_sim_result    wfs_sim_query_events(wfs_sim_handle* h, const char* query_json,
                                      char* out_buf, size_t buf_size, size_t* out_len);
wfs_sim_result    wfs_sim_get_state_hash(wfs_sim_handle* h, char out_hex[65]);
wfs_sim_result    wfs_sim_save(wfs_sim_handle* h, const char* path);
wfs_sim_result    wfs_sim_load_save(wfs_sim_handle* h, const char* path);
```

## 规则

- 错误以 `wfs_sim_result` 错误码返回，禁止静默吞错（宪法第 17 条）；
- 快照为只读 JSON 结构，调用方负责缓冲生命周期；
- `threads` 只影响性能，不影响状态哈希（多线程确定性分区并行）；
- ABI 版本号 `wfs_sim_version` 防 C# 端与核心错配；
- 细节实现见 `sim/src/c_api.cpp`（T012）。

## 事件日志查询（FR-044）

`wfs_sim_query_events` 把事件日志按过滤器查询为只读 JSON 文本，供表现层实现回看、过滤、搜索、置顶与分页。`query_json` 为 JSON 对象，四个字段全部可选：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `category` | string | 分类稳定名称：`command` / `combat` / `intel` / `mission` / `logistics` / `system`；缺省或 `null` = 不过滤；未知名称返回 `INVALID_DATA` |
| `min_severity` | string | 最低严重级稳定名称：`debug` / `info` / `warning` / `critical`（含本级及以上）；缺省或 `null` = 不过滤；未知名称返回 `INVALID_DATA` |
| `text` | string | 区分大小写的消息子串搜索；空串或 `null` = 不过滤 |
| `limit` | 非负整数 | 返回条数上限；`0` 或缺省 = 不限制 |

未知字段忽略（前向兼容）；任何字段类型错误或负数 `limit` 返回 `INVALID_DATA`。过滤条件可任意组合。

成功（`OK`）时 `out_buf` 内容为：

```json
{
  "events": [
    {"seq": 0, "tick": 0, "category": "command", "severity": "info", "message": "COMMAND_QUEUED seq=0 tick=0"}
  ],
  "count": 42,
  "truncated": true
}
```

- `events`：匹配事件数组，按 `seq` 严格升序（追加顺序），`tick` 非降序；
  `seq`/`tick` 为无符号整数，`category`/`severity` 为稳定名称字符串，`message` 为事件正文。
- `count`：过滤后、截断前的总条数（`limit` 截断不影响该值）。
- `truncated`：`limit > 0` 且过滤后总数超过 `limit` 时为 `true`，否则为 `false`。

错误码语义：

- `INVALID_ARGUMENT`：`h`、`query_json`、`out_buf` 或 `out_len` 为空指针；
- `INVALID_DATA`：`query_json` 不是合法 JSON 对象、含未知分类/严重级名称、字段类型错误或 `limit` 为负/非整数；
- `BUFFER_TOO_SMALL`：`out_buf` 不足以容纳文本（含 NUL），`out_len` 写出所需字节数（含 NUL），调用方可按该值扩容后重试（两段式读取，与 `wfs_sim_get_snapshot` 一致）；
- `INTERNAL_ERROR`：序列化等内部失败。

查询只读、不改变模拟状态，连续相同查询输出字节级一致（宪法第 7 条）；缓冲生命周期由调用方负责；同一句柄并发调用由调用方串行化（与全部 `wfs_sim_*` 一致）。
