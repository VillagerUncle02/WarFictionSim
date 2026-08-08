param(
    [string]$Branch = "",
    [int]$TimeoutSeconds = 900,
    [int]$PollSeconds = 10,
    [int]$RunAppearWaitSeconds = 120
)

# 说明：用 Continue 而非 Stop——本脚本大量调用 gh/git 等原生命令，
# Windows PowerShell 5.1 会把原生 stderr 当作 ErrorRecord，Stop 模式会误终止。
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Branch) { $Branch = git branch --show-current }
if (-not $Branch) { Write-Host "ERROR: 无法确定当前分支，请用 -Branch 指定"; exit 2 }

# 0) gh 认证预检：凭据（keyring）不可访问或未登录时立即报错退出，避免静默空转
$null = gh auth status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: gh 未认证或无法访问凭据（keyring）。"
    Write-Host "请在已登录 gh 的环境（沙箱外/escalated）执行；确认方式：gh auth status"
    exit 3
}

$headSha = (git rev-parse HEAD).Trim()
Write-Host "== 等待 CI：分支 $Branch @ $($headSha.Substring(0,7)) =="
Write-Host "run 出现窗口：$RunAppearWaitSeconds 秒；总超时：$TimeoutSeconds 秒；轮询间隔：$PollSeconds 秒"

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$appearDeadline = (Get-Date).AddSeconds($RunAppearWaitSeconds)
$runId = $null
$run = $null

# 1) 等待与"当前分支 + 当前 HEAD"匹配的 CI run 出现（推送后 Actions 需数秒创建）
while ((Get-Date) -lt $appearDeadline) {
    $runsJson = gh run list --branch $Branch --limit 20 --json databaseId,status,conclusion,workflowName,headSha 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: gh run list 失败（网络/权限）。请确认在沙箱外执行且 gh 已登录。"
        exit 3
    }
    try { $runs = $runsJson | ConvertFrom-Json } catch { $runs = @() }
    $run = $runs | Where-Object { $_.workflowName -eq "CI" -and $_.headSha -eq $headSha } | Select-Object -First 1
    if ($run) { $runId = $run.databaseId; break }
    Start-Sleep -Seconds $PollSeconds
}

if (-not $runId) {
    Write-Host "ERROR: 在 $RunAppearWaitSeconds 秒内未找到与当前 HEAD ($($headSha.Substring(0,7))) 匹配的 CI run。"
    Write-Host "可能原因："
    Write-Host "  1) 本次改动只涉及非代码路径（ci.yml 的 paths 过滤跳过了 CI）——若确认是纯文档改动，可跳过等待；"
    Write-Host "  2) 仓库 Actions 未启用、排队超时；"
    Write-Host "  3) 推送未成功（git push 可能因凭据问题挂起，确认在沙箱外执行）。"
    Write-Host "可尝试手动触发：gh workflow run ci.yml --ref $Branch"
    exit 2
}

Write-Host "找到 CI run #$runId，等待完成……"

# 2) 等待 run 完成
while ((Get-Date) -lt $deadline) {
    $runJson = gh run view $runId --json status,conclusion,displayTitle,url 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: gh run view 失败（网络/权限）。"
        exit 3
    }
    try { $run = $runJson | ConvertFrom-Json } catch { $run = $null }
    if ($run.status -eq "completed") { break }
    Start-Sleep -Seconds $PollSeconds
}
if (-not $run -or $run.status -ne "completed") {
    Write-Host "ERROR: 等待 CI run #$runId 超时（状态：$($run.status)）。可用 -TimeoutSeconds 调大后重试。"
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
