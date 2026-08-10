#!/usr/bin/env pwsh
# merge-rebase-next.ps1 — 前序 PR 合并后，将后续链式分支 rebase 到 origin/<Base> 并重新推送
#
# 用法：pwsh merge-rebase-next.ps1 [-Branch <branch>] [-Base main]
# 退出码：0 = 成功；1 = 失败；2 = 冲突（需人工处理）

[CmdletBinding()]
param(
    [string]$Branch = "",
    [string]$Base = "main"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 1 }
if (-not $Branch) { $Branch = git -C $repoRoot branch --show-current }
if (-not $Branch) { Write-Err "ERROR: 无法确定分支，请用 -Branch 指定。"; exit 1 }

if (-not (Test-WorktreeClean $repoRoot)) {
    Write-Err "ERROR: 工作区有未提交改动，先提交/暂存再 rebase。"
    exit 1
}

git -C $repoRoot fetch origin $Base
if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: git fetch 失败。"; exit 1 }

Write-Host "== rebase $Branch -> origin/$Base =="
$rbOut = git -C $repoRoot rebase "origin/$Base" 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Err "ERROR: rebase 失败。"
    Write-UntrackedConflictHint ($rbOut | Out-String)
    Write-Err "若是 rebase 冲突：请人工解决后继续 git rebase --continue，然后重新运行本脚本（或直接 push --force-with-lease）。"
    exit 2
}

Write-Host "== 推送（--force-with-lease） =="
git -C $repoRoot push --force-with-lease origin $Branch
if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: 推送失败。"; exit 1 }

Write-Host "== $Branch 已 rebase 并推送，等待 CI 重新反馈 =="
exit 0
