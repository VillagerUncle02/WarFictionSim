#!/usr/bin/env pwsh
# wait-ci.ps1 — 等待当前分支 + 当前 HEAD 的 GitHub Actions CI run 完成并输出反馈
#
# 用法：
#   pwsh wait-ci.ps1 [-Branch <branch>] [-WorkflowName CI] [-TimeoutSeconds 1800]
#                    [-PollSeconds 15] [-RunAppearWaitSeconds 120] [-GhCallTimeoutSeconds 30]
#
# 退出码：0 = CI 通过；1 = CI 失败（已输出失败 job/日志）；2 = 超时/未触发；3 = gh 认证/网络/连续超时

[CmdletBinding()]
param(
    [string]$Branch = "",
    [string]$WorkflowName = "",
    [int]$TimeoutSeconds = 1800,
    [int]$PollSeconds = 15,
    [int]$RunAppearWaitSeconds = 120,
    [int]$GhCallTimeoutSeconds = 30
)

# 说明：用 Continue 而非 Stop——本脚本大量调用 gh/git 等原生命令，
# Windows PowerShell 5.1 会把原生 stderr 当作 ErrorRecord，Stop 模式会误终止。
$ErrorActionPreference = "Continue"
. "$PSScriptRoot/common.ps1"

$repoRoot = Find-RepoRoot
if (-not $repoRoot) { Write-Host "ERROR: 未找到仓库根（缺少 .specify/）"; exit 3 }
Set-Location $repoRoot

if (-not $Branch) { $Branch = git branch --show-current }
if (-not $Branch) { Write-Host "ERROR: 无法确定当前分支，请用 -Branch 指定"; exit 2 }

# 从 git remote 推导 owner/repo（Start-Job 子进程不在仓库目录，gh 需显式 --repo）
$script:ghRepo = Get-GitRemoteRepo $repoRoot
if (-not $script:ghRepo) { Write-Host "ERROR: 无法从 git remote 推导 owner/repo"; exit 3 }

$script:ghExitCode = 0
$script:ghTimeoutCount = 0

# gh 调用统一走 Start-Job 硬超时：即使网络卡死，单次调用最多 GhCallTimeoutSeconds 秒。
# 注意：job 进程内必须先设置 UTF-8 输出编码，否则 gh 返回的中文（如提交信息）会被
# Windows PowerShell 5.1 按 GBK 解码成坏字节，破坏 JSON 解析。
function Invoke-Gh {
    param([string]$ArgsText)
    $job = Start-Job -ScriptBlock {
        param($a)
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
        $OutputEncoding = [System.Text.Encoding]::UTF8
        $o = & gh @($a -split ' ') 2>&1
        Write-Output ("__GH_EXIT__=" + $LASTEXITCODE)
        if ($null -ne $o) { Write-Output $o }
    } -ArgumentList $ArgsText
    if (-not (Wait-Job $job -Timeout $GhCallTimeoutSeconds)) {
        Stop-Job $job
        Remove-Job $job -Force
        $script:ghTimeoutCount++
        Write-Host "WARN: gh 调用超时（>${GhCallTimeoutSeconds}s）：gh $ArgsText（累计 $($script:ghTimeoutCount) 次）"
        return $null
    }
    $out = Receive-Job $job
    Remove-Job $job -Force
    if ($null -eq $out) { $out = @() }
    if ($out -is [string]) { $out = @($out) }
    $exitLine = $out | Where-Object { $_ -match '^__GH_EXIT__=(\d+)$' } | Select-Object -First 1
    if ($exitLine) {
        $script:ghExitCode = [int]($exitLine -replace '__GH_EXIT__=', '')
        $out = @($out | Where-Object { $_ -notmatch '^__GH_EXIT__=' })
    } else {
        $script:ghExitCode = 1
    }
    return $out
}

# 从输出中提取 JSON 行（对象以 { 开头、数组以 [ 开头；忽略可能的警告/杂讯行）
function Get-JsonObject {
    param($Lines)
    $jsonLine = $Lines | Where-Object { $_.Trim().StartsWith('{') -or $_.Trim().StartsWith('[') } | Select-Object -First 1
    if (-not $jsonLine) { return $null }
    try { return ($jsonLine | ConvertFrom-Json) } catch { return $null }
}

Write-Host "== 等待 CI：分支 $Branch @ $((git rev-parse HEAD).Trim().Substring(0,7)) =="
Write-Host "workflow: $WorkflowName | run 出现窗口：$RunAppearWaitSeconds 秒；总超时：$TimeoutSeconds 秒；gh 单次调用硬超时：$GhCallTimeoutSeconds 秒"

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$appearDeadline = (Get-Date).AddSeconds($RunAppearWaitSeconds)
$runId = $null
$run = $null

# 0) gh 认证预检（带硬超时）：未登录/凭据不可访问时立即退出，不空转
$null = Invoke-Gh "auth status"
if ($script:ghExitCode -ne 0 -or $script:ghTimeoutCount -gt 0) {
    Write-Host "ERROR: gh 未认证或无法访问凭据（keyring）/调用超时。"
    Write-Host "请在已登录 gh 的环境（沙箱外/escalated）执行；确认方式：gh auth status"
    exit 3
}

# 1) 等待与"当前分支 + 当前 HEAD"匹配的 CI run 出现（推送后 Actions 需数秒创建）
$headSha = (git rev-parse HEAD).Trim()
while ((Get-Date) -lt $appearDeadline) {
    $runsJson = Invoke-Gh "run list --repo $script:ghRepo --branch $Branch --limit 20 --json databaseId,status,conclusion,workflowName,headSha"
    if ($null -eq $runsJson) {
        if ($script:ghTimeoutCount -ge 3) {
            Write-Host "ERROR: gh 连续超时 3 次，退出。"
            exit 3
        }
        Start-Sleep -Seconds $PollSeconds
        continue
    }
    if ($script:ghExitCode -ne 0) {
        Write-Host "ERROR: gh run list 失败（网络/权限）。请确认在沙箱外执行且 gh 已登录。"
        exit 3
    }
    $runs = Get-JsonObject $runsJson
    if (-not $runs) { $runs = @() }
    # WorkflowName 为空时按"分支 + 当前 HEAD"匹配任意 workflow
    $run = $runs | Where-Object { ($_.headSha -eq $headSha) -and (-not $WorkflowName -or $_.workflowName -eq $WorkflowName) } | Select-Object -First 1
    if ($run) { $runId = $run.databaseId; break }
    Start-Sleep -Seconds $PollSeconds
}

if (-not $runId) {
    Write-Host "ERROR: 在 $RunAppearWaitSeconds 秒内未找到与当前 HEAD ($($headSha.Substring(0,7))) 匹配的 CI run（workflow: $WorkflowName）。"
    Write-Host "可能原因："
    Write-Host "  1) 本次改动只涉及非代码路径（workflow 的 paths 过滤跳过了 CI）——若确认是纯文档改动，可跳过等待；"
    Write-Host "  2) 仓库 Actions 未启用、排队超时；"
    Write-Host "  3) 推送未成功（git push 可能因凭据问题挂起，确认在沙箱外执行）。"
    Write-Host "可尝试手动触发：gh workflow run $WorkflowName --ref $Branch"
    exit 2
}

Write-Host "找到 CI run #$runId，等待完成……"

# 2) 等待 run 完成（轮询只请求 ASCII 字段 status/conclusion，避免中文编码破坏 JSON）
while ((Get-Date) -lt $deadline) {
    $runJson = Invoke-Gh "run view --repo $script:ghRepo $runId --json status,conclusion"
    if ($null -eq $runJson) {
        if ($script:ghTimeoutCount -ge 3) {
            Write-Host "ERROR: gh 连续超时 3 次，退出。"
            exit 3
        }
        Start-Sleep -Seconds $PollSeconds
        continue
    }
    if ($script:ghExitCode -ne 0) {
        Write-Host "ERROR: gh run view 失败（网络/权限）。"
        exit 3
    }
    $run = Get-JsonObject $runJson
    if ($null -ne $run -and $run.status -eq "completed") { break }
    Start-Sleep -Seconds $PollSeconds
}
if ($null -eq $run -or $run.status -ne "completed") {
    Write-Host "ERROR: 等待 CI run #$runId 超时（状态：$($run.status)）。可用 -TimeoutSeconds 调大后重试。"
    exit 2
}

# 完成后取展示信息（中文 title 经 UTF-8 编码修复后应可正常解析；失败则降级只显示 run id）
$meta = Get-JsonObject (Invoke-Gh "run view --repo $script:ghRepo $runId --json displayTitle,url")
$displayTitle = if ($meta) { $meta.displayTitle } else { "run #$runId" }
$runUrl = if ($meta) { $meta.url } else { "https://github.com/$script:ghRepo/actions/runs/$runId" }

Write-Host "CI 结论：$($run.conclusion) | $displayTitle"
Write-Host "run 链接：$runUrl"

if ($run.conclusion -eq "success") {
    Write-Host "== CI 通过 =="
    exit 0
}

# 3) 失败：列出失败 job/step 与失败日志（同样带硬超时）
Write-Host "== CI 失败，失败详情 =="
$viewOut = Invoke-Gh "run view --repo $script:ghRepo $runId"
if ($null -ne $viewOut) { $viewOut | Out-String | Write-Host }
Write-Host "---- 失败步骤日志（--log-failed）----"
$logOut = Invoke-Gh "run view --repo $script:ghRepo $runId --log-failed"
if ($null -ne $logOut) { $logOut | Out-String | Write-Host }
exit 1
