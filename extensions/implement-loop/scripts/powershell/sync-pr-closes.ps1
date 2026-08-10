#!/usr/bin/env pwsh
# sync-pr-closes.ps1 — 保持 open PR 正文的 Closes # 与 tasks.md 已完成任务同步
#
# 用法：
#   pwsh sync-pr-closes.ps1 [-TasksFile <tasks.md>] [-Repo owner/repo] [-PR <编号>] [-DryRun]
#
# 行为：
#   - 收集 tasks.md 中所有 [X] 任务，通过 issue 标题映射真实 issue 号；
#   - 读取当前分支 open PR 正文，移除旧 Closes 行，末尾追加完整 Closes 块；
#   - 正文无变化时不改；有变化则 gh pr edit --body（除非 -DryRun）。

[CmdletBinding()]
param(
    [string]$TasksFile = "",
    [string]$Repo = "",
    [string]$PR = "",
    [string]$TitlePattern = "^T\d{3,}",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 1 }
if (-not $Repo) { $Repo = Get-GitRemoteRepo $repoRoot }
if (-not $Repo) { Write-Err "ERROR: 无法从 git remote 推导 owner/repo，请用 -Repo 指定。"; exit 1 }
if (-not $TasksFile) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "specs") -Recurse -Filter "tasks.md" -ErrorAction SilentlyContinue)
    if ($candidates.Count -eq 1) { $TasksFile = $candidates[0].FullName }
    else { Write-Err "ERROR: 未提供 -TasksFile 且无法唯一确定 tasks.md。"; exit 2 }
}
if (-not (Test-Path -LiteralPath $TasksFile -PathType Leaf)) {
    Write-Err "ERROR: tasks.md 不存在：$TasksFile"
    exit 2
}

# 当前分支 open PR
$branch = git -C $repoRoot branch --show-current
if (-not $PR) {
    $prOut = (& gh pr list --repo $Repo --head $branch --state open --json number 2>$null) | Out-String
    if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: gh pr list 失败（网络/权限）。"; exit 2 }
    try {
        $prs = @($prOut | ConvertFrom-Json)
        if ($prs.Count -gt 0) { $PR = [string]$prs[0].number }
    } catch { }
}
if (-not $PR) {
    Write-Host "当前分支 $branch 没有 open PR，无需同步 Closes。"
    exit 0
}

# 已完成任务 → issue 映射
$taskIds = @(Get-TaskIdsFromTasksFile -TasksFile $TasksFile -CompletedOnly)
if ($taskIds.Count -eq 0) {
    Write-Host "tasks.md 中没有已完成（[X]）任务，无需同步 Closes。"
    exit 0
}
$issueMap = Get-TaskIssueMap -Repo $Repo -TitlePattern $TitlePattern
if ($null -eq $issueMap) {
    Write-Err "ERROR: 无法拉取 issue 列表（gh api 失败）。"
    exit 2
}
$closesNums = @()
$unmapped = @()
foreach ($id in $taskIds) {
    if ($issueMap.ContainsKey($id)) { $closesNums += $issueMap[$id] } else { $unmapped += $id }
}
$closesNums = @($closesNums | Sort-Object -Unique)
if ($unmapped.Count -gt 0) {
    Write-Host "WARN: 以下已完成任务未找到对应 issue：$($unmapped -join ', ')"
}
$closesLines = if ($closesNums.Count -gt 0) { ($closesNums | ForEach-Object { "Closes #$_" }) -join "`n" } else { "" }

# 读取现有正文并规范化 Closes 块
$viewOut = (& gh pr view $PR --repo $Repo --json body 2>$null) | Out-String
if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: gh pr view $PR 失败（网络/权限）。"; exit 2 }
$body = ""
try { $body = [string](($viewOut | ConvertFrom-Json).body) } catch { }
$newBody = [regex]::Replace($body, '(?m)^\s*Closes\s+#\d+\s*$', '')
$newBody = $newBody.TrimEnd()
if ($closesLines) { $newBody = $newBody + "`n`n" + $closesLines }

if ($newBody.Trim() -eq $body.Trim()) {
    Write-Host "PR #$PR 正文 Closes 已是最新（$($closesNums.Count) 个）。"
    exit 0
}

if ($DryRun) {
    Write-Host "== DryRun：PR #$PR 正文将更新 =="
    Write-Host "closes: $($closesNums -join ', ')"
    Write-Host "---- 新正文 ----"
    Write-Host $newBody
    exit 0
}

$tmp = [System.IO.Path]::GetTempFileName()
[System.IO.File]::WriteAllText($tmp, $newBody, [System.Text.Encoding]::UTF8)
try {
    gh pr edit $PR --repo $Repo --body-file $tmp
    if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: gh pr edit 失败。"; exit 1 }
} finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
}
Write-Host "PR #$PR 正文已同步 Closes：$($closesNums -join ', ')"
exit 0
