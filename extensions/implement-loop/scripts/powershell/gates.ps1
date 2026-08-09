#!/usr/bin/env pwsh
# gates.ps1 — 通用门禁（扩展自带默认；项目可自定义 scripts/gates.ps1 覆盖）
#
# 自动探测并执行：CMake(C/C++) / dotnet(C#) / cargo(Rust) / npm(Node) / pytest(Python)
# 以及格式检查：clang-format / dotnet format / cargo fmt
#
# 用法：pwsh gates.ps1 [-Quick]
#   -Quick：仅构建 + 测试，跳过格式检查与重新配置

[CmdletBinding()]
param([switch]$Quick)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$root = Find-RepoRoot
if (-not $root) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 1 }
Set-Location $root

if ($Quick) { Write-Host "== 快速门禁（构建 + 测试，跳过格式） ==" }
else { Write-Host "== 全量门禁：构建 + 测试 + 格式 ==" }

$hasCpp = Test-Path -LiteralPath "CMakeLists.txt" -PathType Leaf
$hasSln = @(Get-ChildItem -Filter "*.sln" -File -ErrorAction SilentlyContinue).Count -gt 0
$hasCargo = Test-Path -LiteralPath "Cargo.toml" -PathType Leaf
$hasNpm = Test-Path -LiteralPath "package.json" -PathType Leaf
$hasPy = (Test-Path -LiteralPath "pyproject.toml" -PathType Leaf) -or (Test-Path -LiteralPath "setup.py" -PathType Leaf) -or (Test-Path -LiteralPath "requirements.txt" -PathType Leaf)

# ---- 1. C/C++（CMake） ----
if ($hasCpp) {
    Write-Host "[1] C/C++：CMake + CTest"
    if (-not (Test-Path -LiteralPath "build\CMakeCache.txt")) {
        cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }
    }
    cmake --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "C++ 构建失败" }
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest 失败" }
} else { Write-Host "[1] 跳过 C/C++（无 CMakeLists.txt）" }

# ---- 2. C#（.NET） ----
if ($hasSln) {
    if (Get-Command dotnet -ErrorAction SilentlyContinue) {
        Write-Host "[2] C#：dotnet build + test"
        dotnet build . -c Release
        if ($LASTEXITCODE -ne 0) { throw "dotnet 构建失败" }
        dotnet test . -c Release --no-build
        if ($LASTEXITCODE -ne 0) { throw "dotnet 测试失败" }
    } else {
        Write-Host "[2] 跳过 C#（dotnet 不可用）"
    }
} else { Write-Host "[2] 跳过 C#（无 *.sln）" }

# ---- 3. Rust ----
if ($hasCargo) {
    if (Get-Command cargo -ErrorAction SilentlyContinue) {
        Write-Host "[3] Rust：cargo test"
        cargo test --all-features
        if ($LASTEXITCODE -ne 0) { throw "cargo test 失败" }
    } else {
        Write-Host "[3] 跳过 Rust（cargo 不可用）"
    }
} else { Write-Host "[3] 跳过 Rust（无 Cargo.toml）" }

# ---- 4. Node ----
if ($hasNpm) {
    if (Get-Command npm -ErrorAction SilentlyContinue) {
        try {
            $pkg = Get-Content -LiteralPath "package.json" -Raw | ConvertFrom-Json
            if ($pkg.scripts.test) {
                Write-Host "[4] Node：npm test"
                npm test
                if ($LASTEXITCODE -ne 0) { throw "npm test 失败" }
            } else {
                Write-Host "[4] 跳过 Node（package.json 无 test 脚本）"
            }
        } catch {
            Write-Host "[4] 跳过 Node（package.json 解析失败：$($_.Exception.Message)）"
        }
    } else {
        Write-Host "[4] 跳过 Node（npm 不可用）"
    }
} else { Write-Host "[4] 跳过 Node（无 package.json）" }

# ---- 5. Python ----
if ($hasPy) {
    if (Get-Command pytest -ErrorAction SilentlyContinue) {
        Write-Host "[5] Python：pytest"
        pytest -q
        if ($LASTEXITCODE -ne 0) { throw "pytest 失败" }
    } else {
        Write-Host "[5] 跳过 Python（pytest 不可用）"
    }
} else { Write-Host "[5] 跳过 Python（未检测到 pyproject.toml/setup.py/requirements.txt）" }

# ---- 6. 格式检查（仅全量） ----
if ($Quick) {
    Write-Host "[6] 跳过格式（快速模式）"
} else {
    Write-Host "[6] 格式检查"
    $fmtIssues = 0
    if (Test-Path -LiteralPath ".clang-format" -PathType Leaf) {
        if (Get-Command clang-format -ErrorAction SilentlyContinue) {
            $src = @(Get-ChildItem -Recurse -Include *.cpp,*.cc,*.cxx,*.h,*.hpp,*.c -File -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -notmatch '\\(build|dist|node_modules|bin|obj)\\' } |
                ForEach-Object { $_.FullName })
            if ($src.Count -gt 0) {
                clang-format --dry-run --Werror $src
                if ($LASTEXITCODE -ne 0) { $fmtIssues++ }
            } else {
                Write-Host "  clang-format：无源码可检查"
            }
        } else {
            Write-Host "  clang-format 不可用，跳过"
        }
    }
    if ($hasSln -and (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        dotnet format . --verify-no-changes
        if ($LASTEXITCODE -ne 0) { $fmtIssues++ }
    }
    if ($hasCargo -and (Get-Command cargo -ErrorAction SilentlyContinue)) {
        cargo fmt --check
        if ($LASTEXITCODE -ne 0) { $fmtIssues++ }
    }
    if ($fmtIssues -gt 0) { throw "格式检查发现 $fmtIssues 项问题" }
}

Write-Host "== 门禁通过 =="
exit 0
