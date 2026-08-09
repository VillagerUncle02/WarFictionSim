#!/usr/bin/env pwsh
# gates.ps1 — 通用门禁（扩展自带默认；项目可自定义 scripts/gates.ps1 覆盖）
#
# 自动探测并执行：CMake(C/C++) / dotnet(C#) / cargo(Rust) / npm(Node) / pytest(Python)
# / go(Go) / mvn(Maven) / gradle(Gradle)，以及格式检查：clang-format / dotnet format / cargo fmt。
# 配置 gates.steps 的自定义命令自动并入（无需手动传参）；也可用 -Steps 显式传入。
#
# 用法：pwsh gates.ps1 [-Quick]
#   -Quick：仅构建 + 测试，跳过格式检查与重新配置

[CmdletBinding()]
param(
    [switch]$Quick,
    [string[]]$Steps = @()
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$root = Find-RepoRoot
if (-not $root) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 1 }
Set-Location $root

# 未显式传 -Steps 时，从扩展配置读取 gates.steps（与 load-config 同一事实来源）
if (-not $Steps -or $Steps.Count -eq 0) {
    try {
        $cfgJson = & pwsh -NoProfile -File (Join-Path $PSScriptRoot "load-config.ps1") -Json 2>$null
        if ($cfgJson) {
            $stepsCfg = ($cfgJson | ConvertFrom-Json).GATES_STEPS
            if ($stepsCfg) { $Steps = @($stepsCfg) }
        }
    } catch { }
}

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

# ---- 6. Go ----
if (Test-Path -LiteralPath "go.mod" -PathType Leaf) {
    if (Get-Command go -ErrorAction SilentlyContinue) {
        Write-Host "[6] Go：go test ./..."
        go test ./...
        if ($LASTEXITCODE -ne 0) { throw "go test 失败" }
    } else {
        Write-Host "[6] 跳过 Go（go 不可用）"
    }
} else { Write-Host "[6] 跳过 Go（无 go.mod）" }

# ---- 7. Maven ----
if (Test-Path -LiteralPath "pom.xml" -PathType Leaf) {
    if (Get-Command mvn -ErrorAction SilentlyContinue) {
        Write-Host "[7] Maven：mvn -q test"
        mvn -q test
        if ($LASTEXITCODE -ne 0) { throw "mvn test 失败" }
    } else {
        Write-Host "[7] 跳过 Maven（mvn 不可用）"
    }
} else { Write-Host "[7] 跳过 Maven（无 pom.xml）" }

# ---- 8. Gradle ----
$gradleCmd = $null
if (Test-Path -LiteralPath "gradlew.bat" -PathType Leaf) { $gradleCmd = ".\gradlew.bat" }
elseif (Test-Path -LiteralPath "gradlew" -PathType Leaf) { $gradleCmd = "./gradlew" }
elseif ((Test-Path -LiteralPath "build.gradle" -PathType Leaf) -or (Test-Path -LiteralPath "build.gradle.kts" -PathType Leaf)) {
    if (Get-Command gradle -ErrorAction SilentlyContinue) { $gradleCmd = "gradle" }
}
if ($gradleCmd) {
    Write-Host "[8] Gradle：$gradleCmd test"
    & $gradleCmd test
    if ($LASTEXITCODE -ne 0) { throw "gradle test 失败" }
} else {
    Write-Host "[8] 跳过 Gradle（未检测到 gradlew / build.gradle(.kts)）"
}

# ---- 9. 自定义门禁步骤（配置 gates.steps，换语言/工具时的扩展口） ----
$stepNo = 0
foreach ($step in $Steps) {
    $stepNo++
    Write-Host "[9.$stepNo] 自定义门禁步骤：$step"
    cmd /c $step
    if ($LASTEXITCODE -ne 0) { throw "自定义门禁步骤失败（exit $LASTEXITCODE）：$step" }
}
if ($stepNo -eq 0) { Write-Host "[9] 无自定义门禁步骤（gates.steps 为空）" }

# ---- 10. 格式检查（仅全量） ----
if ($Quick) {
    Write-Host "[10] 跳过格式（快速模式）"
} else {
    Write-Host "[10] 格式检查"
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
