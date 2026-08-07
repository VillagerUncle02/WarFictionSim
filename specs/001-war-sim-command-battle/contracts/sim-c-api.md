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
