param(
    [string]$Config = "Release",
    [switch]$Quick
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Quick) {
    Write-Host "== 快速门禁（构建 + 测试，跳过格式） =="
} else {
    Write-Host "== 全量门禁：构建 + 测试 + 格式 =="
}

# C++ 核心（sim/core_c）—— 存在 CMake 时才执行
if (Test-Path "CMakeLists.txt") {
    if (-not $Quick) {
        Write-Host "[1/5] CMake 配置（vcpkg manifest）"
        cmake -B build -S . -DVCPKG_MANIFEST_MODE=ON
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }
    }

    Write-Host "[2/5] C++ 构建（$Config）"
    cmake --build build --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "C++ 构建失败" }

    Write-Host "[3/5] CTest"
    ctest --test-dir build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest 失败" }
} else {
    Write-Host "[1-3/5] 跳过 C++（无 CMakeLists.txt）"
}

# C#（ui/tools）—— 存在 ui 目录时才执行
if (Test-Path "ui") {
    Write-Host "[4/5] dotnet 构建 + 测试"
    dotnet build ui/ -c $Config
    if ($LASTEXITCODE -ne 0) { throw "dotnet 构建失败" }
    dotnet test ui/ -c $Config --no-build
    if ($LASTEXITCODE -ne 0) { throw "dotnet 测试失败" }
} else {
    Write-Host "[4/5] 跳过 C#（无 ui/）"
}

if (-not $Quick) {
    Write-Host "[5/5] 格式检查"
    if (Test-Path "sim") { clang-format --dry-run --Werror (Get-ChildItem sim,core_c -Recurse -Include *.cpp,*.h,*.c | ForEach-Object FullName) }
    if (Test-Path "ui") { dotnet format ui/ --verify-no-changes }
} else {
    Write-Host "[5/5] 跳过格式（快速模式）"
}

Write-Host "== 门禁通过 =="
