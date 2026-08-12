# sim —— C++20 确定性模拟核心（宪法第 15 条：无头可测）
# T008–T014：STATIC 确定性核心源码（core_c / RNG / 时钟 / 队列 / C ABI /
# 场景加载校验 / 命令双重校验）；JSON 与 JSON Schema 校验依赖按 vcpkg 清单
# 锁定版本（宪法第 18 条）。
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

add_library(sim STATIC
    src/rng.cpp
    src/clock.cpp
    src/queue.cpp
    src/event_log.cpp
    src/sha256.cpp
    src/snapshot.cpp
    src/save.cpp
    src/parallel.cpp
    src/loader.cpp
    src/command_validation.cpp
    src/schema_validator.cpp
    src/c_api.cpp
    src/sim_runtime.cpp
    src/ai/decision_log.cpp
    src/ai_inject.cpp
)

target_include_directories(sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sim PUBLIC
    core_c
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
)

if (MSVC)
    target_compile_options(sim PRIVATE /utf-8)
endif()
