#!/usr/bin/env pwsh
# check-pr-order.ps1 — 链式合并顺序检查（供 CI 或人工调用）
#
# 规则：同一时间只允许 1 个活动分支在实现；合并必须按 PR 编号顺序。
# 若存在比当前分支 PR 编号更小的 open PR，则判定顺序未满足。
#
# 用法：pwsh check-pr-order.ps1 [-Branch <branch>] [-Repo owner/repo] [-Json]
# 退出码：0 = 无更早 open PR（顺序满足）；1 = 存在更早 open PR；2 = 当前分支无 PR / 环境错误

[CmdletBinding()]
param(
    [string]$Branch = "",
    [string]$Repo = "",
    [switch]$Json
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 2 }
if (-not $Branch) { $Branch = git -C $repoRoot branch --show-current }
if (-not $Repo) { $Repo = Get-GitRemoteRepo $repoRoot }
if (-not $Repo) { Write-Err "ERROR: 无法推导 owner/repo。"; exit 2 }

$pulls = @()
$out = (& gh api --paginate "repos/$Repo/pulls?state=open&per_page=100&sort=created&direction=asc" 2>$null) | Out-String
if ($LASTEXITCODE -ne 0) {
    Write-Err "ERROR: gh api 拉取 open PR 失败。"
    exit 2
}
try { $pulls = @($out | ConvertFrom-Json) } catch { }

$mine = $pulls | Where-Object { $_.head.ref -eq $Branch } | Select-Object -First 1
if (-not $mine) {
    Write-Host "当前分支 $Branch 没有 open PR，跳过顺序检查。"
    exit 2
}

$earlier = @($pulls | Where-Object { $_.number -lt $mine.number } | Sort-Object number)
if ($earlier.Count -eq 0) {
    if ($Json) {
        [PSCustomObject]@{ ok = $true; current_pr = $mine.number; blocking_prs = @() } | ConvertTo-Json -Compress
    } else {
        Write-Host "== 顺序满足：PR #$($mine.number) 之前无未合并的 open PR =="
    }
    exit 0
}

if ($Json) {
    [PSCustomObject]@{
        ok = $false
        current_pr = $mine.number
        blocking_prs = @($earlier | ForEach-Object { $_.number })
    } | ConvertTo-Json -Compress
} else {
    Write-Host "== 顺序未满足：以下 open PR 应先于当前 PR #$($mine.number) 合并 =="
    $earlier | ForEach-Object { Write-Host "  PR #$($_.number): $($_.title)（分支 $($_.head.ref)）" }
}
exit 1
