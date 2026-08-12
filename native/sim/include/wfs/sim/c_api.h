// sim/include/wfs/sim/c_api.h
//
// T012：C ABI 边界层（P/Invoke 边界）公开接口。
//
// 设计契约（contracts/sim-c-api.md；宪法第 14 条）：
// - sim 核心对外只暴露纯 C ABI：C# 通过 P/Invoke 消费，不共享 C++ 对象、
//   不共享可变状态；跨层只传纯数据（字符串/字节缓冲/错误码）。
// - 不透明句柄 wfs_sim_handle：生命周期由 wfs_sim_create/destroy 管理，
//   调用方不得访问内部成员。
// - 错误一律以 wfs_sim_result 错误码返回，禁止静默吞错（宪法第 17 条）；
//   create 无错误码通道，失败时显式返回 nullptr。
// - 快照缓冲生命周期：调用方负责分配与释放 out_buf（纯数据 JSON 文本）；
//   缓冲区不足返回 WFS_SIM_RESULT_BUFFER_TOO_SMALL 并写出所需字节数。
// - wfs_sim_version 返回 ABI 版本字符串，C# 端用于检测核心错配。
// - 本头文件同时兼容 C 与 C++（extern "C"），由 c_api.cpp 实现。
// - 非线程安全：同一句柄的并发调用必须由调用方串行化（与 EventQueue 一致，
//   并发注入边界由 T020 落实）。

#ifndef WFS_SIM_C_API_H_
#define WFS_SIM_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WFS_SIM_ABI_VERSION_MAJOR 0
#define WFS_SIM_ABI_VERSION_MINOR 1
#define WFS_SIM_ABI_VERSION_PATCH 0
#define WFS_SIM_VERSION_STRING "0.1.0"
// SHA-256 十六进制文本长度 64 + NUL（contracts/save-format.md：state_hash）。
#define WFS_SIM_STATE_HASH_HEX_LEN 65

typedef struct wfs_sim_handle wfs_sim_handle;

typedef enum wfs_sim_result {
    WFS_SIM_RESULT_OK = 0,
    WFS_SIM_RESULT_INVALID_ARGUMENT = 1,
    WFS_SIM_RESULT_IO_ERROR = 2,
    WFS_SIM_RESULT_INVALID_DATA = 3,
    WFS_SIM_RESULT_BUFFER_TOO_SMALL = 4,
    WFS_SIM_RESULT_NOT_IMPLEMENTED = 5,
    WFS_SIM_RESULT_INTERNAL_ERROR = 6
} wfs_sim_result;

// 创建模拟句柄：加载并校验场景（T013），失败返回 NULL。
// threads 只影响性能不影响状态哈希（research.md §1）；threads < 1 视为非法参数。
wfs_sim_handle* wfs_sim_create(const char* scenario_path, uint64_t seed, int threads);

// 销毁句柄；NULL 为无操作。
void wfs_sim_destroy(wfs_sim_handle* h);

// 返回 ABI 版本字符串（静态存储，无需释放）。
const char* wfs_sim_version(void);

// 推进一个离散 tick，并按 (tick, seq) 顺序处理到期命令（T011 队列）。
wfs_sim_result wfs_sim_step(wfs_sim_handle* h);

// 注入玩家命令：经 T014 双重校验后入队；校验失败不改变队列状态。
wfs_sim_result wfs_sim_inject_command(wfs_sim_handle* h, const char* command_json);

// 注入 AI 决策：与玩家命令共用同一结构/校验/队列（FR-045/069）。
wfs_sim_result wfs_sim_inject_ai_decision(wfs_sim_handle* h, const char* decision_json);

// 输出只读 JSON 快照文本到调用方缓冲；out_len 为文本字节数（不含 NUL）。
wfs_sim_result wfs_sim_get_snapshot(wfs_sim_handle* h, char* out_buf, size_t buf_size, size_t* out_len);

// 输出状态哈希（SHA-256，T016）：对确定性状态 JSON 序列计算摘要，
// 线程数只影响性能、不影响哈希结果（宪法第 7 条）。
wfs_sim_result wfs_sim_get_state_hash(wfs_sim_handle* h, char out_hex[WFS_SIM_STATE_HASH_HEX_LEN]);

// 存档写入/读取（T017）：WFS-SAVE 格式（magic + format_version +
// header_json + state_blob + state_hash），见 contracts/save-format.md。
wfs_sim_result wfs_sim_save(wfs_sim_handle* h, const char* path);
wfs_sim_result wfs_sim_load_save(wfs_sim_handle* h, const char* path);

#ifdef __cplusplus
}
#endif

#endif  // WFS_SIM_C_API_H_
