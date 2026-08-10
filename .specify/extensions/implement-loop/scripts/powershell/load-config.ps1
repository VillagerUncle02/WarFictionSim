#!/usr/bin/env pwsh
# load-config.ps1 — 解析 implement-loop 扩展配置，输出运行所需的全部路径与参数（JSON）
#
# 用法：
#   pwsh load-config.ps1 [-Feature <dir>] [-Json]
#
# 配置优先级（低 -> 高）：
#   内置默认值 < 自动探测（仓库事实，仅未显式配置时） <
#   .specify/extensions/implement-loop/implement-loop-config.yml
#   < implement-loop-config.local.yml < 环境变量 SPECKIT_IMPLEMENT_LOOP_*
#   < -Feature 命令行参数

[CmdletBinding()]
param(
    [string]$Feature = "",
    [switch]$Json
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) {
    Write-Err "ERROR: 未找到仓库根（缺少 .specify/ 目录）。请从 spec-kit 项目目录内运行。"
    exit 1
}

$extRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$extId = "implement-loop"

# ---------- 1. 默认配置 ----------
$cfg = @{
    "language"                        = "zh-CN"
    "feature.directory"               = ""
    "execution.assignments_file"      = "agent-assignments.yml"
    "execution.parallel"              = $true
    "execution.devops_agent"          = ""
    "branch.prefix"                   = "feature/"
    "branch.base"                     = "main"
    "branch.chained"                  = $true
    "ci.workflow_file"                = "ci.yml"
    "ci.workflow_name"                = "CI"
    "ci.wait_timeout_seconds"         = 1800
    "ci.require_push_trigger"         = $true
    "gates.script"                    = ""
    "gates.quick_on_demand"           = $true
    "notes.reviews_dir"               = "notes/reviews"
    "review.code_reviewer"            = ""
    "review.devops_opinion"           = ""
    "review.max_rounds"               = 5
    "review.convergence_warn_rounds"  = 4
    "review.second_opinion"           = $false
    "review.second_opinion_triggers"  = "确定性核心, 并发, 跨语言边界, 安全"
    "github.require_issues"           = $true
    "github.issue_title_pattern"      = "^T\d{3}"
    "github.pr_body_template"         = ""
}

# ---------- 2. 解析扁平的 dotted-key YAML 子集 ----------
function Read-FlatYaml {
    param([string]$Path)
    $result = @{}
    foreach ($raw in Get-Content -LiteralPath $Path) {
        $line = $raw.Trim()
        if (-not $line -or $line.StartsWith("#")) { continue }
        # 去掉不在引号内的行尾注释
        $line = [regex]::Replace($line, '("[^"]*")|(\x27[^\x27]*\x27)|(\s+#.*$)', {
            param($m)
            if ($m.Groups[3].Success) { return "" } else { return $m.Value }
        })
        $line = $line.Trim()
        if (-not $line) { continue }
        $m = [regex]::Match($line, '^([A-Za-z0-9_.\-]+):\s*(.*)$')
        if (-not $m.Success) { continue }
        $key = $m.Groups[1].Value
        $value = $m.Groups[2].Value.Trim()
        # 去引号
        if ($value.Length -ge 2 -and
            (($value.StartsWith('"') -and $value.EndsWith('"')) -or
             ($value.StartsWith("'") -and $value.EndsWith("'")))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        if ($value -eq "") { $result[$key] = ""; continue }
        if ($value -ieq "true") { $result[$key] = $true; continue }
        if ($value -ieq "false") { $result[$key] = $false; continue }
        if ($value -match '^-?\d+$') { $result[$key] = [int]$value; continue }
        $result[$key] = $value
    }
    return $result
}

$configDir = Join-Path $repoRoot (".specify\extensions\$extId")
$explicitKeys = @{}
foreach ($file in @(
        (Join-Path $configDir "implement-loop-config.yml"),
        (Join-Path $configDir "implement-loop-config.local.yml"))) {
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        $parsed = Read-FlatYaml $file
        foreach ($k in $parsed.Keys) {
            if ($cfg.ContainsKey($k)) {
                $cfg[$k] = $parsed[$k]
                $explicitKeys[$k] = $true
            }
        }
    }
}

# ---------- 3. 环境变量覆盖（SPECKIT_IMPLEMENT_LOOP_<KEY>，点转下划线、大写） ----------
# 注意：遍历键快照，避免在枚举期间修改哈希表触发 "Collection was modified"
foreach ($k in @($cfg.Keys)) {
    $envName = "SPECKIT_IMPLEMENT_LOOP_" + ($k -replace "\.", "_").ToUpper()
    $envVal = [Environment]::GetEnvironmentVariable($envName)
    if ($null -ne $envVal -and $envVal -ne "") {
        if ($envVal -ieq "true") { $cfg[$k] = $true }
        elseif ($envVal -ieq "false") { $cfg[$k] = $false }
        elseif ($envVal -match '^-?\d+$') { $cfg[$k] = [int]$envVal }
        else { $cfg[$k] = $envVal }
        $explicitKeys[$k] = $true
    }
}

# ---------- 3.5 自动探测（仅针对未显式配置的键） ----------
# 目的：换项目不把本项目（WarFictionSim）的宪法/流程/角色当作隐性前提。
# 只探测"仓库里能读到的确定事实"；探测不到的值为空，
# 由使用扩展的 AI 在运行时检查项目后确定（必要时询问用户）。

# 3.5.1 branch.base：从 origin/HEAD 探测默认分支（无需网络）；探测不到则为空
if (-not $explicitKeys.ContainsKey("branch.base")) {
    $headRef = git -C $repoRoot symbolic-ref refs/remotes/origin/HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $headRef -match 'refs/heads/(.+)$') {
        $cfg["branch.base"] = $Matches[1]
    } else {
        $cfg["branch.base"] = ""
    }
}

# 3.5.2 ci.workflow_file：.github/workflows 下优先 ci.yml，其次唯一 workflow
if (-not $explicitKeys.ContainsKey("ci.workflow_file")) {
    $wfDir = Join-Path $repoRoot ".github\workflows"
    $wfFiles = @()
    if (Test-Path -LiteralPath $wfDir -PathType Container) {
        $wfFiles = @(Get-ChildItem -LiteralPath $wfDir -Filter *.yml -File -ErrorAction SilentlyContinue) +
            @(Get-ChildItem -LiteralPath $wfDir -Filter *.yaml -File -ErrorAction SilentlyContinue)
    }
    if (Test-Path -LiteralPath (Join-Path $wfDir "ci.yml") -PathType Leaf) {
        $cfg["ci.workflow_file"] = "ci.yml"
    } elseif ($wfFiles.Count -eq 1) {
        $cfg["ci.workflow_file"] = $wfFiles[0].Name
    } else {
        $cfg["ci.workflow_file"] = ""
    }
}

# 3.5.3 ci.workflow_name：读取 workflow 文件的 name: 字段；读取不到则为空
if (-not $explicitKeys.ContainsKey("ci.workflow_name")) {
    $cfg["ci.workflow_name"] = ""
    $wfFile = $cfg["ci.workflow_file"]
    if ($wfFile) {
        $wfPath = Join-Path $repoRoot (".github\workflows\$wfFile")
        if (Test-Path -LiteralPath $wfPath -PathType Leaf) {
            $nameLine = Select-String -LiteralPath $wfPath -Pattern '^name:\s*(.+)$' | Select-Object -First 1
            if ($nameLine) {
                $nameVal = $nameLine.Matches[0].Groups[1].Value.Trim().Trim('"').Trim("'")
                if ($nameVal) { $cfg["ci.workflow_name"] = $nameVal }
            }
        }
    }
}

# ---------- 4. 解析 feature 目录 ----------
$featureDir = $cfg["feature.directory"]
if ($Feature) { $featureDir = $Feature }
if ($featureDir) {
    if (-not [System.IO.Path]::IsPathRooted($featureDir)) {
        $featureDir = Join-Path $repoRoot $featureDir
    }
} elseif (Test-Path -LiteralPath (Join-Path $repoRoot ".specify\feature.json") -PathType Leaf) {
    try {
        $fj = Get-Content -LiteralPath (Join-Path $repoRoot ".specify\feature.json") -Raw | ConvertFrom-Json
        if ($fj.feature_directory) {
            $featureDir = Join-Path $repoRoot $fj.feature_directory
        }
    } catch { }
}
if (-not $featureDir -or -not (Test-Path -LiteralPath $featureDir -PathType Container)) {
    # 回退：扫描 specs/*/tasks.md，唯一时自动选中
    $specsRoot = Join-Path $repoRoot "specs"
    $candidates = @()
    if (Test-Path -LiteralPath $specsRoot -PathType Container) {
        $candidates = @(Get-ChildItem -LiteralPath $specsRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "tasks.md") -PathType Leaf })
    }
    if ($candidates.Count -eq 1) {
        $featureDir = $candidates[0].FullName
    } elseif ($candidates.Count -gt 1) {
        Write-Err "ERROR: 发现多个候选 feature 目录（$($candidates.FullName -join '; ')）。请在配置中设置 feature.directory 或使用 -Feature 参数。"
        exit 1
    } else {
        Write-Err "ERROR: 无法确定 feature 目录（无 .specify/feature.json，且 specs/ 下没有含 tasks.md 的目录）。请先运行 speckit.specify / speckit.tasks，或在配置中设置 feature.directory。"
        exit 1
    }
}
$featureDir = (Resolve-Path -LiteralPath $featureDir).Path
$featureName = Split-Path $featureDir -Leaf

# ---------- 5. 解析脚本路径（项目自定义优先，否则扩展自带） ----------
function Resolve-Script {
    param([string]$ProjectPath, [string]$ExtPath)
    if ($ProjectPath -and (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) { return (Resolve-Path -LiteralPath $ProjectPath).Path }
    return (Resolve-Path -LiteralPath $ExtPath).Path
}

# GATES_SCRIPT 解析：配置 gates.script > 项目 <repo>/scripts/gates.ps1 > 空
# （为空时由使用扩展的 AI 参照 templates/gates-template.ps1 现场编写，经用户确认）
$gatesScript = $cfg["gates.script"]
if ($gatesScript) {
    if (Test-Path -LiteralPath $gatesScript -PathType Leaf) {
        $gatesScript = (Resolve-Path -LiteralPath $gatesScript).Path
    } else {
        Write-Err "WARN: 配置的 gates.script 不存在：$gatesScript（按未配置处理，由 AI 现场编写）"
        $gatesScript = ""
    }
} elseif (Test-Path -LiteralPath (Join-Path $repoRoot "scripts\gates.ps1") -PathType Leaf) {
    $gatesScript = (Resolve-Path -LiteralPath (Join-Path $repoRoot "scripts\gates.ps1")).Path
} else {
    $gatesScript = ""
}
$openPrScript = Resolve-Script (Join-Path $repoRoot "scripts\open-pr.ps1") (Join-Path $extRoot "scripts\powershell\open-pr.ps1")
$waitCiScript = Resolve-Script (Join-Path $repoRoot "scripts\wait-ci.ps1") (Join-Path $extRoot "scripts\powershell\wait-ci.ps1")
$extScripts = Join-Path $extRoot "scripts\powershell"
$checkIssuesScript = (Resolve-Path -LiteralPath (Join-Path $extScripts "check-issues.ps1")).Path
$syncPrClosesScript = (Resolve-Path -LiteralPath (Join-Path $extScripts "sync-pr-closes.ps1")).Path
$prepareBranchScript = (Resolve-Path -LiteralPath (Join-Path $extScripts "prepare-branch.ps1")).Path
$mergeRebaseScript = (Resolve-Path -LiteralPath (Join-Path $extScripts "merge-rebase-next.ps1")).Path
$checkPrOrderScript = (Resolve-Path -LiteralPath (Join-Path $extScripts "check-pr-order.ps1")).Path

# ---------- 6. 组装输出 ----------
$assignFile = $cfg["execution.assignments_file"]
$reviewsDir = $cfg["notes.reviews_dir"]
if (-not [System.IO.Path]::IsPathRooted($reviewsDir)) { $reviewsDir = Join-Path $repoRoot $reviewsDir }

$out = [ordered]@{
    "REPO_ROOT"                  = $repoRoot
    "EXT_DIR"                    = $extRoot
    "FEATURE_DIR"                = $featureDir
    "FEATURE_NAME"               = $featureName
    "SPEC_FILE"                  = (Join-Path $featureDir "spec.md")
    "PLAN_FILE"                  = (Join-Path $featureDir "plan.md")
    "TASKS_FILE"                 = (Join-Path $featureDir "tasks.md")
    "DATA_MODEL_FILE"            = (Join-Path $featureDir "data-model.md")
    "CONTRACTS_DIR"              = (Join-Path $featureDir "contracts")
    "RESEARCH_FILE"              = (Join-Path $featureDir "research.md")
    "QUICKSTART_FILE"            = (Join-Path $featureDir "quickstart.md")
    "ASSIGNMENTS_FILE"           = $assignFile
    "ASSIGNMENTS_PATH"           = (Join-Path $featureDir $assignFile)
    "LANGUAGE"                   = $cfg["language"]
    "PARALLEL"                   = [bool]$cfg["execution.parallel"]
    "DEVOPS_AGENT"               = $cfg["execution.devops_agent"]
    "BRANCH_PREFIX"              = $cfg["branch.prefix"]
    "BRANCH_BASE"                = $cfg["branch.base"]
    "CHAINED"                    = [bool]$cfg["branch.chained"]
    "CI_WORKFLOW_FILE"           = $cfg["ci.workflow_file"]
    "CI_WORKFLOW_NAME"           = $cfg["ci.workflow_name"]
    "CI_WAIT_TIMEOUT_SECONDS"    = [int]$cfg["ci.wait_timeout_seconds"]
    "CI_REQUIRE_PUSH_TRIGGER"    = [bool]$cfg["ci.require_push_trigger"]
    "GATES_SCRIPT"               = $gatesScript
    "GATES_TEMPLATE"             = (Join-Path $extRoot "templates\gates-template.ps1")
    "OPEN_PR_SCRIPT"             = $openPrScript
    "WAIT_CI_SCRIPT"             = $waitCiScript
    "CHECK_ISSUES_SCRIPT"        = $checkIssuesScript
    "SYNC_PR_CLOSES_SCRIPT"      = $syncPrClosesScript
    "PREPARE_BRANCH_SCRIPT"      = $prepareBranchScript
    "MERGE_REBASE_SCRIPT"        = $mergeRebaseScript
    "CHECK_PR_ORDER_SCRIPT"      = $checkPrOrderScript
    "REVIEWS_DIR"                = $reviewsDir
    "REVIEWER"                   = $cfg["review.code_reviewer"]
    "DEVOPS_OPINION"             = $cfg["review.devops_opinion"]
    "MAX_ROUNDS"                 = [int]$cfg["review.max_rounds"]
    "CONVERGENCE_WARN_ROUNDS"    = [int]$cfg["review.convergence_warn_rounds"]
    "SECOND_OPINION"             = [bool]$cfg["review.second_opinion"]
    "SECOND_OPINION_TRIGGERS"    = @($cfg["review.second_opinion_triggers"] -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    "REQUIRE_ISSUES"             = [bool]$cfg["github.require_issues"]
    "ISSUE_TITLE_PATTERN"        = $cfg["github.issue_title_pattern"]
    "PR_BODY_TEMPLATE"           = $cfg["github.pr_body_template"]
    "CURRENT_BRANCH"             = (git -C $repoRoot branch --show-current 2>$null)
}

if ($Json) {
    $out | ConvertTo-Json -Compress
} else {
    $out.GetEnumerator() | ForEach-Object { Write-Output ("{0}: {1}" -f $_.Key, $_.Value) }
}
