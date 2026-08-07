param(
    [string]$Branch = "",
    [int]$TimeoutSeconds = 1800,
    [int]$PollSeconds = 10
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Branch) { $Branch = git branch --show-current }
if (-not $Branch) { throw "无法确定当前分支，请用 -Branch 指定" }

Write-Host "== 等待 CI：分支 $Branch =="
Write-Host "超时：$TimeoutSeconds 秒；轮询间隔：$PollSeconds 秒"

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$runId = $null
$run = $null

# 1) 等待该分支出现 CI run（推送后 Actions 需要数秒创建 run）
while ((Get-Date) -lt $deadline) {
    $runs = gh run list --branch $Branch --limit 20 --json databaseId,status,conclusion,workflowName 2>$null | ConvertFrom-Json
    $run = $runs | Where-Object { $_.workflowName -eq "CI" } | Select-Object -First 1
    if ($run) { $runId = $run.databaseId; break }
    Start-Sleep -Seconds $PollSeconds
}
if (-not $runId) {
    Write-Host "ERROR: 超时未找到 CI run（分支 $Branch）。请检查 ci.yml 的 push 触发条件与仓库 Actions 状态；可尝试 gh workflow run ci.yml --ref $Branch 手动触发。"
    exit 2
}

Write-Host "找到 CI run #$runId，等待完成……"

# 2) 等待 run 完成
while ((Get-Date) -lt $deadline) {
    $run = gh run view $runId --json status,conclusion,displayTitle,url 2>$null | ConvertFrom-Json
    if ($run.status -eq "completed") { break }
    Start-Sleep -Seconds $PollSeconds
}
if ($run.status -ne "completed") {
    Write-Host "ERROR: 等待 CI run #$runId 超时（状态：$($run.status)）。"
    exit 2
}

Write-Host "CI 结论：$($run.conclusion) | $($run.displayTitle)"
Write-Host "run 链接：$($run.url)"

if ($run.conclusion -eq "success") {
    Write-Host "== CI 通过 =="
    exit 0
}

# 3) 失败：列出失败 job/step 与失败日志
Write-Host "== CI 失败，失败详情 =="
gh run view $runId 2>&1 | Out-String | Write-Host
Write-Host "---- 失败步骤日志（--log-failed）----"
gh run view $runId --log-failed 2>&1 | Out-String | Write-Host
exit 1
