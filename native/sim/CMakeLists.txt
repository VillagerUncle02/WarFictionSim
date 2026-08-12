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
    src/ai/script_backend.cpp
    src/ai_inject.cpp
    src/headless_driver.cpp
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

# ---- T019：无头 CLI（宪法 15：无头可测；quickstart §3）----
add_executable(sim_headless headless/main.cpp)
target_link_libraries(sim_headless PRIVATE sim)
# 内部驱动/注入头文件（sim_runtime.h、ai_inject.h、sim_state.h）。
target_include_directories(sim_headless PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if (MSVC)
    target_compile_options(sim_headless PRIVATE /utf-8)
endif()

# Windows 下 json-schema-validator 为动态库时，把运行期 DLL 复制到
# sim_headless.exe 目录（与 sim_tests 同一策略），避免运行时找不到 DLL。
if (WIN32)
    set(_sim_headless_need_dll_copy FALSE)
    get_target_property(_sim_headless_location nlohmann_json_schema_validator::validator IMPORTED_LOCATION_RELEASE)
    if (NOT _sim_headless_location OR _sim_headless_location MATCHES "-NOTFOUND$")
        get_target_property(_sim_headless_location nlohmann_json_schema_validator::validator IMPORTED_LOCATION)
    endif()
    if (_sim_headless_location)
        string(TOLOWER "${_sim_headless_location}" _sim_headless_location_lower)
        if (_sim_headless_location_lower MATCHES "\\.dll$")
            set(_sim_headless_need_dll_copy TRUE)
        endif()
    endif()
    if (_sim_headless_need_dll_copy)
        add_custom_command(TARGET sim_headless POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:sim_headless>
                $<TARGET_FILE_DIR:sim_headless>
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
    unset(_sim_headless_need_dll_copy)
    unset(_sim_headless_location)
    unset(_sim_headless_location_lower)
endif()

# ---- sim_core：C# 桥接 DLL（contracts/sim-c-api.md，宪法第 14 条） ----
# C ABI 边界源码（src/c_api.cpp）已就绪时构建共享库；C# 侧通过
# app/Directory.Build.targets 从 native/build/clang-cl-<config>/ 复制本 DLL。
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/c_api.cpp")
    add_library(sim_core SHARED src/c_api.cpp)
    target_compile_definitions(sim_core PRIVATE WFS_SIM_CORE_EXPORTS)
    target_include_directories(sim_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(sim_core PRIVATE sim)
    # 过渡策略：c_api.h 尚未带导出宏时先导出 DLL 全部符号（C ABI 按名
    # P/Invoke）；后续可在 c_api.h 增加 WFS_SIM_API 后移除本属性。
    set_target_properties(sim_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if (MSVC)
        target_compile_options(sim_core PRIVATE /utf-8)
    endif()
    if (WIN32)
        # 运行期依赖 DLL（如 json-schema-validator）复制到 sim_core.dll 同目录；
        # 与 sim_tests 同策略：按 IMPORTED_LOCATION 扩展名判断动态/静态，
        # 避免静态 triplet 下 $<TARGET_RUNTIME_DLLS> 空展开导致命令缺参报错。
        set(_sim_core_need_dll_copy FALSE)
        get_target_property(_sim_core_dep_location nlohmann_json_schema_validator::validator IMPORTED_LOCATION_RELEASE)
        if (NOT _sim_core_dep_location OR _sim_core_dep_location MATCHES "-NOTFOUND$")
            get_target_property(_sim_core_dep_location nlohmann_json_schema_validator::validator IMPORTED_LOCATION)
        endif()
        if (_sim_core_dep_location)
            string(TOLOWER "${_sim_core_dep_location}" _sim_core_dep_location_lower)
            if (_sim_core_dep_location_lower MATCHES "\\.dll$")
                set(_sim_core_need_dll_copy TRUE)
            endif()
        endif()
        if (_sim_core_need_dll_copy)
            add_custom_command(TARGET sim_core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:sim_core>
                    $<TARGET_FILE_DIR:sim_core>
                COMMAND_EXPAND_LISTS
                VERBATIM)
        endif()
        unset(_sim_core_need_dll_copy)
        unset(_sim_core_dep_location)
        unset(_sim_core_dep_location_lower)
    endif()
endif()
