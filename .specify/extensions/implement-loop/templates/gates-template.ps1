# gates.ps1 —— 项目门禁样板
#
# 本文件是"样板"：当项目没有 scripts/gates.ps1 时，由使用
# speckit-implement-loop 扩展的 AI 检查本项目实际使用的语言/工具链后，
# 复制本文件并按实际情况填写命令，保存到 <仓库根>/scripts/gates.ps1，
# 经用户确认后执行并随功能分支提交（之后即为该项目的门禁脚本）。
#
# 填写指引（AI 在运行时检查项目后确定，不要凭空猜测）：
#   - 构建/测试：看 CI 工作流（.github/workflows/*.yml）里的构建/测试步骤，
#     本地照抄对应命令；没有 CI 则看项目的构建文件
#     （CMakeLists.txt / *.sln / Cargo.toml / package.json / pyproject.toml /
#      go.mod / pom.xml / build.gradle(.kts) / Makefile …）
#   - 格式：看项目是否有 .clang-format / .editorconfig / 代码风格配置
#   - 某一步在本机不可用时打印"跳过"并说明原因，不要静默失败或伪造通过
#
# 约定：
#   - 不传 -Quick：全量门禁（构建 + 测试 + 格式）
#   - -Quick：仅构建 + 测试（跳过格式，用于审查修复后的快速验证）
#   - 任何一步失败立即以非 0 退出，不要继续后面的步骤

[CmdletBinding()]
param([switch]$Quick)

$ErrorActionPreference = "Stop"

# ---- 1. 构建 ----
# 示例（按项目实际情况保留/替换）：
#   cmake --build build --config Release --parallel
#   dotnet build WarFictionSim.sln -c Release
#   cargo build --release
#   go build ./...
#   npm run build

# ---- 2. 测试 ----
# 示例：
#   ctest --test-dir build -C Release --output-on-failure
#   dotnet test WarFictionSim.sln -c Release --no-build
#   cargo test --all-features
#   go test ./...
#   npm test
#   pytest -q

# ---- 3. 格式检查（仅全量模式） ----
if (-not $Quick) {
    # 示例：
    #   clang-format --dry-run --Werror <源码文件>
    #   dotnet format WarFictionSim.sln --verify-no-changes
    #   cargo fmt --check
    #   gofmt -l .
}

Write-Host "== 门禁通过 =="
