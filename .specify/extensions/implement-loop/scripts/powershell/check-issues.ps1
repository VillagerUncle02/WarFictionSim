#!/usr/bin/env pwsh
# check-issues.ps1 — 前置检查：tasks.md 中每个任务 ID（T###）是否都有对应 GitHub issue
#
# 用法：
#   pwsh check-issues.ps1 -TasksFile <tasks.md> [-Repo owner/repo] [-TitlePattern "^T\d{3}"] [-Json]
#
# 退出码：0 = 全部有对应 issue；1 = 存在缺失；2 = 参数/环境错误

[CmdletBinding()]
param(
    [string]$TasksFile = "",
    [string]$Repo = "",
    [string]$TitlePattern = "^T\d{3}",
    [switch]$Json
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 2 }
if (-not $TasksFile) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "specs") -Recurse -Filter "tasks.md" -ErrorAction SilentlyContinue)
    if ($candidates.Count -eq 1) { $TasksFile = $candidates[0].FullName }
    else { Write-Err "ERROR: 未提供 -TasksFile 且无法唯一确定 tasks.md。"; exit 2 }
}
if (-not (Test-Path -LiteralPath $TasksFile -PathType Leaf)) {
    Write-Err "ERROR: tasks.md 不存在：$TasksFile"
    exit 2
}
if (-not $Repo) { $Repo = Get-GitRemoteRepo $repoRoot }
if (-not $Repo) { Write-Err "ERROR: 无法从 git remote 推导 owner/repo，请用 -Repo 指定。"; exit 2 }

# 解析任务 ID
$ids = @()
foreach ($line in Get-Content -LiteralPath $TasksFile) {
    if ($line -match '^\s*- \[[ xX]\]\s+(T\d{3})\b') {
        $ids += $Matches[1]
    }
}
$ids = @($ids | Select-Object -Unique)
if ($ids.Count -eq 0) {
    Write-Err "ERROR: tasks.md 中未解析到任何任务 ID（格式：- [ ] T### ...）。"
    exit 2
}

# 拉取 open issues 标题（gh issue list 输出单一 JSON 数组，天然排除 PR）
$ghOut = (& gh issue list --repo $Repo --state open --limit 1000 --json number,title 2>$null) | Out-String
if ($LASTEXITCODE -ne 0) {
    Write-Err "ERROR: gh issue list 拉取失败（网络/权限）。请确认在已登录 gh 的环境执行。"
    exit 2
}
$issues = @()
try { $issues = @($ghOut | ConvertFrom-Json) } catch { }
$issueTitles = @($issues | ForEach-Object { $_.title })

$pattern = if ($TitlePattern) { $TitlePattern } else { "" }
$missing = @()
foreach ($id in $ids) {
    $hit = $issueTitles | Where-Object {
        $t = $_
        ($t -match [regex]::Escape($id)) -and
        (-not $pattern -or $t -match $pattern)
    } | Select-Object -First 1
    if (-not $hit) { $missing += $id }
}

if ($Json) {
    [PSCustomObject]@{
        total = $ids.Count
        missing = $missing
        issue_titles_fetched = $issueTitles.Count
    } | ConvertTo-Json -Compress
} else {
    Write-Host "任务 ID 总数：$($ids.Count)；已拉取 open issue 标题：$($issueTitles.Count)"
    if ($missing.Count -eq 0) {
        Write-Host "== 全部任务均有对应 issue =="
    } else {
        Write-Host "缺失 issue 的任务：$($missing -join ', ')"
    }
}

if ($missing.Count -gt 0) { exit 1 } else { exit 0 }
