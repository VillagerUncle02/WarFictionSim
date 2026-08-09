#!/usr/bin/env pwsh
# prepare-branch.ps1 — 实现循环第 3 步：工作区检查 + 创建/检出链式功能分支
#
# 用法：
#   pwsh prepare-branch.ps1 -Branch feature/us1-mvp [-Base main] [-Chained] [-Tip <commit|branch>]
#
# 行为：
#   - 工作区有已跟踪改动 -> 退出 1（由人工处理）
#   - 分支已存在 -> 检出并报告与基线的差异（不自动合并/重置）
#   - 分支不存在 -> 从 origin/<Base>（或 -Tip）创建

[CmdletBinding()]
param(
    [string]$Branch = "",
    [string]$Base = "main",
    [switch]$Chained,
    [string]$Tip = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Err "ERROR: 未找到仓库根（缺少 .specify/）。"; exit 1 }
if (-not $Branch) {
    Write-Err "ERROR: 必须提供 -Branch（如 feature/us1-mvp）。"
    exit 1
}

if (-not (Test-WorktreeClean $repoRoot)) {
    Write-Err "ERROR: 工作区有未提交的已跟踪改动。请先提交、暂存或丢弃，再创建分支。"
    Write-Err "（未跟踪文件不阻塞，可自行确认是否带入新分支）"
    exit 1
}

Write-Host "== 同步基线 origin/$Base =="
git -C $repoRoot fetch origin $Base
if ($LASTEXITCODE -ne 0) {
    Write-Err "ERROR: git fetch origin $Base 失败（可能需要网络/凭据）。"
    exit 1
}

$exists = git -C $repoRoot rev-parse --verify --quiet "refs/heads/$Branch"
if ($LASTEXITCODE -eq 0) {
    Write-Host "分支已存在：检出 $Branch"
    git -C $repoRoot checkout $Branch
    if ($LASTEXITCODE -ne 0) { Write-Err "ERROR: 检出失败"; exit 1 }
    $behind = git -C $repoRoot rev-list --count "HEAD..origin/$Base" 2>$null
    if ($LASTEXITCODE -eq 0 -and [int]$behind -gt 0) {
        Write-Host "提示：$Branch 落后 origin/$Base $behind 个提交，未自动合并；如需同步请人工处理（如 git merge --ff-only origin/$Base）。"
    } else {
        Write-Host "$Branch 与 origin/$Base 一致。"
    }
    exit 0
}

$basePoint = if ($Chained -and $Tip) { $Tip } else { "origin/$Base" }
Write-Host "创建分支 $Branch（基点：$basePoint）"
git -C $repoRoot checkout -b $Branch $basePoint
if ($LASTEXITCODE -ne 0) {
    Write-Err "ERROR: 创建分支失败（基点不存在或其它 git 错误）。"
    exit 1
}

Write-Host "== 分支就绪：$Branch @ $((git -C $repoRoot rev-parse --short HEAD)) =="
exit 0
