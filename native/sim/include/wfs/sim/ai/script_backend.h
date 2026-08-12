// sim/include/wfs/sim/ai/script_backend.h
//
// T021：无 AI 脚本后端公开接口。
//
// 设计契约（spec FR-068/069/SC-007；宪法第 8/9 条）：
// - 脚本后端是确定性非 AI 兜底：同一决策输入必然产生同一命令 JSON（纯函数，
//   不使用模拟 RNG、不读取现实时钟、不依赖线程数），输出与 LLM 后端同构
//   （同一条命令 JSON 结构），经 T014 双重校验后由注入通道执行。
// - create_script_backend 返回 IAiBackend 实例；name() == "script"。
// - script_decision_nodes 供无头驱动/测试复用"哪些节点需要脚本决策"的规则：
//   按场景单位数组首次出现顺序枚举非玩家节点（每节点单一 AI 归属，FR-048）。

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "wfs/sim/ai/ia_backend.h"
#include "wfs/sim/loader.h"

namespace wfs::sim {

// 创建确定性脚本 AI 后端（无网络、无密钥；离线兜底路径）。
std::unique_ptr<IAiBackend> create_script_backend();

// 脚本模式下应产生决策的指挥节点：排除玩家节点，按单位数组顺序去重。
std::vector<std::string> script_decision_nodes(const Scenario& scenario);

}  // namespace wfs::sim
