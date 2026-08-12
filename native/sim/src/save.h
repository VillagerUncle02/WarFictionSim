// sim/src/save.h
//
// 内部共享：存档写入/读取接口（T017），由 c_api.cpp 的
// wfs_sim_save / wfs_sim_load_save 委托调用。

#pragma once

#include <filesystem>

#include "wfs/sim/c_api.h"

#include "sim_state.h"

namespace wfs::sim {

// 把当前状态写入 WFS-SAVE 格式文件（临时文件 + 原子替换，不产生半写存档）。
wfs_sim_result save_to_file(const SimState& state, const std::filesystem::path& path);

// 从存档恢复状态；校验失败（magic/版本/哈希/元数据不匹配）返回
// WFS_SIM_RESULT_INVALID_DATA 且不修改 state（强保证）。
wfs_sim_result load_save_into(SimState& state, const std::filesystem::path& path);

}  // namespace wfs::sim
