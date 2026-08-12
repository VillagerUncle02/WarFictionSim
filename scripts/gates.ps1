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

# C++ 核心（native/sim、native/core_c）—— 存在 CMake 时才执行
$nativeRoot = Join-Path $root "native"
if (Test-Path (Join-Path $nativeRoot "CMakeLists.txt")) {
    if (-not $env:VCPKG_ROOT) {
        if (Test-Path "C:/vcpkg") { $env:VCPKG_ROOT = "C:/vcpkg" }
        elseif (Test-Path "C:/Users/ASUS/vcpkg") { $env:VCPKG_ROOT = "C:/Users/ASUS/vcpkg" }
    }
    $preset = "clang-cl-$($Config.ToLower())"
    if (-not $Quick) {
        Write-Host "[1/5] CMake 配置（$preset）"
        Push-Location $nativeRoot
        cmake --preset $preset
        $cmakeExit = $LASTEXITCODE
        Pop-Location
        if ($cmakeExit -ne 0) { throw "CMake 配置失败" }
    }

    Write-Host "[2/5] C++ 构建（$preset）"
    Push-Location $nativeRoot
    cmake --build --preset $preset
    $cmakeExit = $LASTEXITCODE
    Pop-Location
    if ($cmakeExit -ne 0) { throw "C++ 构建失败" }

    Write-Host "[3/5] CTest"
    Push-Location $nativeRoot
    ctest --preset $preset
    $cmakeExit = $LASTEXITCODE
    Pop-Location
    if ($cmakeExit -ne 0) { throw "CTest 失败" }
} else {
    Write-Host "[1-3/5] 跳过 C++（无 native/CMakeLists.txt）"
}

# C#（app/ui、app/tools）—— 存在解决方案且本机装有 .NET 10 SDK 时才执行（CI 会自动安装）
$sdk10 = (dotnet --list-sdks 2>$null | Select-String '^10\.')
$appSln = Join-Path $root "app/WarFictionSim.sln"
if ((Test-Path $appSln) -and $sdk10) {
    Write-Host "[4/5] dotnet 构建 + 测试"
    dotnet build $appSln -c $Config
    if ($LASTEXITCODE -ne 0) { throw "dotnet 构建失败" }
    dotnet test $appSln -c $Config --no-build
    if ($LASTEXITCODE -ne 0) { throw "dotnet 测试失败" }
} elseif (Test-Path $appSln) {
    Write-Host "[4/5] 跳过 C#（本机未安装 .NET 10 SDK；CI 会安装）"
} else {
    Write-Host "[4/5] 跳过 C#（无解决方案）"
}

if (-not $Quick) {
    Write-Host "[5/5] 格式检查"
    if (Test-Path (Join-Path $root "native")) {
        $cppFiles = @(Get-ChildItem (Join-Path $root "native/sim"),(Join-Path $root "native/core_c") -Recurse -Include *.cpp,*.h,*.c -ErrorAction SilentlyContinue | ForEach-Object FullName)
        if ($cppFiles.Count -gt 0) {
            clang-format --dry-run --Werror $cppFiles
            if ($LASTEXITCODE -ne 0) { throw "clang-format 格式检查失败" }
        } else {
            Write-Host "无 C++ 源码，跳过 clang-format"
        }
    }
    if ((Test-Path $appSln) -and $sdk10) {
        dotnet format style $appSln --verify-no-changes
        if ($LASTEXITCODE -ne 0) { throw "dotnet format style 失败" }
        dotnet format analyzers $appSln --verify-no-changes
        if ($LASTEXITCODE -ne 0) { throw "dotnet format analyzers 失败" }
    } elseif (Test-Path $appSln) {
        Write-Host "跳过 dotnet format（本机未安装 .NET 10 SDK）"
    }
} else {
    Write-Host "[5/5] 跳过格式（快速模式）"
}

Write-Host "== 门禁通过 =="
