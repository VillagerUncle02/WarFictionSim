# tests/cli_tests/run_support_chain_tests.ps1
#
# T045 支援请求链路集成测试 runner（宪法 15：无头可测；宪法 7：确定性）。
#
# 覆盖（tasks.md T045；SC-004；FR-005/008/009/010）：
# - 连排级有限分数：受理后分数扣减明确可见（score_cost/score_remaining），
#   超出剩余分数时拒绝（INSUFFICIENT_SCORE），任务结束触发归建事件序列；
# - 营级配属链（确定性裁决桩，替代 US3 营级 AI，已登记待办）：配属/拒绝/
#   转请与归建事件序列；
# - 连排级与营级同输入两次运行状态哈希与事件序列一致（确定性回归）。
#
# 所有断言失败以非零退出码结束（宪法 17：不静默吞错）。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$ScenarioPlatoon,
    [Parameter(Mandatory = $true)][string]$ScenarioBattalion,
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Seed = "7",
    [string]$Ticks = "1200"
)

$ErrorActionPreference = 'Stop'
$tempDir = Join-Path $env:TEMP ("wfs-support-chain-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

function Fail([string]$Message) {
    Write-Host "SUPPORT CHAIN TEST FAILED: $Message"
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    exit 1
}

function Invoke-Cli([string[]]$ArgumentList) {
    $output = & $Exe @ArgumentList 2>&1
    $code = $LASTEXITCODE
    return [pscustomobject]@{ Code = $code; Output = ($output -join "`n") }
}

function Last-Line([string]$Text) {
    $lines = @($Text -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })
    if ($lines.Count -eq 0) { return "" }
    return $lines[-1].Trim()
}

function Read-Events([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Fail "缺少 JSONL 输出文件: $Path"
    }
    $events = @()
    $prevSeq = -1
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line.Trim().Length -eq 0) { continue }
        try {
            $obj = $line | ConvertFrom-Json
        } catch {
            Fail "JSONL 行不是合法 JSON: $line"
        }
        foreach ($key in @('seq', 'tick', 'category', 'severity', 'message')) {
            if ($null -eq $obj.$key) {
                Fail "JSONL 行缺少字段 ${key}: $line"
            }
        }
        $seq = [long]$obj.seq
        if ($seq -le $prevSeq) {
            Fail "JSONL seq 非严格升序: $seq（前序 $prevSeq）"
        }
        $prevSeq = $seq
        $events += [pscustomobject]@{ Seq = $seq; Tick = [long]$obj.tick; Message = [string]$obj.message }
    }
    if ($events.Count -eq 0) {
        Fail "JSONL 输出为空: $Path"
    }
    return $events
}

function Match-Events($Events, [string]$Pattern) {
    return @($Events | Where-Object { $_.Message -match $Pattern })
}

function Assert-Match($Events, [string]$Pattern, [string]$What) {
    $found = Match-Events $Events $Pattern
    if ($found.Count -eq 0) {
        Fail "缺少事件：$What（模式 $Pattern）"
    }
    return $found
}

function Assert-Sequence($Events, [string[]]$Patterns, [string]$What) {
    $lastSeq = -1
    foreach ($pattern in $Patterns) {
        $found = Match-Events $Events $pattern
        if ($found.Count -eq 0) {
            Fail "序列断言缺少事件（$What）：$pattern"
        }
        if ($found[0].Seq -le $lastSeq) {
            Fail "事件顺序错误（$What）：$pattern 早于前序事件"
        }
        $lastSeq = $found[0].Seq
    }
}

function Extract([string]$Message, [string]$Pattern) {
    if ($Message -notmatch $Pattern) { return "" }
    return $Matches[1]
}

# ---- 连排级有限分数路径 ----
$jsonl = Join-Path $tempDir 'platoon.jsonl'
$r = Invoke-Cli @('inject', '--scenario', $ScenarioPlatoon, '--seed', $Seed, '--threads', '1', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl, '--hash')
if ($r.Code -ne 0) {
    Fail "连排级 inject 退出码 $($r.Code): $($r.Output)"
}
$hashFirst = Last-Line $r.Output
$events = Read-Events $jsonl

$jsonl2 = Join-Path $tempDir 'platoon2.jsonl'
$r2 = Invoke-Cli @('inject', '--scenario', $ScenarioPlatoon, '--seed', $Seed, '--threads', '2', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl2, '--hash')
if ($r2.Code -ne 0) {
    Fail "连排级 inject 第二次运行退出码 $($r2.Code): $($r2.Output)"
}
$hashSecond = Last-Line $r2.Output
$events2 = Read-Events $jsonl2
if ($hashFirst -ne $hashSecond) {
    Fail "连排级同输入两次运行状态哈希不一致"
}
$messagesFirst = @($events | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
$messagesSecond = @($events2 | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
if (($messagesFirst -join "`n") -ne ($messagesSecond -join "`n")) {
    Fail "连排级同输入两次运行事件序列不一致"
}

# 三条命令都必须下达。
foreach ($cmdId in @('cmd-0', 'cmd-1', 'cmd-2')) {
    Assert-Match $events "^COMMAND_ISSUED command=$($cmdId) " "下达事件 $cmdId"
}

# cmd-1 成功：提交 → 评估 → 配属（扣分 30、剩余 30 明确可见，interaction 标签强制）。
Assert-Sequence $events @(
    '^SUPPORT_REQUESTED request=req-cmd-1 ',
    '^SUPPORT_EVALUATING request=req-cmd-1 ',
    '^SUPPORT_ASSIGNED request=req-cmd-1 '
) '连排级成功请求'
$requested = Assert-Match $events '^SUPPORT_REQUESTED request=req-cmd-1 ' '连排级请求登记'
if ($requested[0].Message -notmatch 'interaction=SUPPORT_REQUEST') {
    Fail "SUPPORT_REQUESTED 必须携带 interaction=SUPPORT_REQUEST: $($requested[0].Message)"
}
$assigned = Assert-Match $events '^SUPPORT_ASSIGNED request=req-cmd-1 ' '连排级配属事件'
if ($assigned[0].Message -notmatch 'score_cost=30' -or $assigned[0].Message -notmatch 'score_remaining=30') {
    Fail "连排级扣分必须明确可见（cost=30/remaining=30）: $($assigned[0].Message)"
}
if ($assigned[0].Message -notmatch 'units=\[squad-mortar-team\]') {
    Fail "配属单位清单错误: $($assigned[0].Message)"
}

# cmd-2 拒绝：分数不足（用尽即止），原因明确。
Assert-Sequence $events @(
    '^SUPPORT_REQUESTED request=req-cmd-2 ',
    '^SUPPORT_EVALUATING request=req-cmd-2 ',
    '^SUPPORT_REJECTED request=req-cmd-2 '
) '连排级拒绝请求'
$rejected = Assert-Match $events '^SUPPORT_REJECTED request=req-cmd-2 ' '连排级拒绝事件'
if ($rejected[0].Message -notmatch 'INSUFFICIENT_SCORE') {
    Fail "连排级拒绝原因应为 INSUFFICIENT_SCORE: $($rejected[0].Message)"
}

# 目标任务完成 → 支援归建（任务结束归建，SC-004）。
Assert-Sequence $events @(
    '^SUPPORT_ASSIGNED request=req-cmd-1 ',
    'MISSION_COMPLETED unit=sp-squad-1',
    '^ATTACH_RETURNING request=req-cmd-1 ',
    '^ATTACH_RETURNED request=req-cmd-1 '
) '连排级归建序列'
$returning = Assert-Match $events '^ATTACH_RETURNING request=req-cmd-1 ' '连排级归建启动'
if ($returning[0].Message -notmatch 'unit=squad-mortar-team') {
    Fail "归建单位必须为配属单位: $($returning[0].Message)"
}

# ---- 营级配属链路径（确定性裁决桩替代 US3 营级 AI） ----
$bnJsonl = Join-Path $tempDir 'battalion.jsonl'
# 营级场景脚本为空脚本（仅注释行）：请求由场景数据 scripted_requests 确定性触发。
$bnScript = Join-Path $tempDir 'empty.jsonl'
"# 营级配属链由场景 scripted_requests 驱动（US3 AI 裁决桩，TODO 已登记）" | Out-File -FilePath $bnScript -Encoding utf8
$rBn = Invoke-Cli @('inject', '--scenario', $ScenarioBattalion, '--seed', $Seed, '--threads', '1', '--ticks', $Ticks,
    '--script', $bnScript, '--out', $bnJsonl, '--hash')
if ($rBn.Code -ne 0) {
    Fail "营级 inject 退出码 $($rBn.Code): $($rBn.Output)"
}
$bnHashFirst = Last-Line $rBn.Output
$bnEvents = Read-Events $bnJsonl

$bnJsonl2 = Join-Path $tempDir 'battalion2.jsonl'
$rBn2 = Invoke-Cli @('inject', '--scenario', $ScenarioBattalion, '--seed', $Seed, '--threads', '2', '--ticks', $Ticks,
    '--script', $bnScript, '--out', $bnJsonl2, '--hash')
if ($rBn2.Code -ne 0) {
    Fail "营级 inject 第二次运行退出码 $($rBn2.Code): $($rBn2.Output)"
}
$bnHashSecond = Last-Line $rBn2.Output
$bnEvents2 = Read-Events $bnJsonl2
if ($bnHashFirst -ne $bnHashSecond) {
    Fail "营级同输入两次运行状态哈希不一致"
}
$bnMessagesFirst = @($bnEvents | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
$bnMessagesSecond = @($bnEvents2 | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
if (($bnMessagesFirst -join "`n") -ne ($bnMessagesSecond -join "`n")) {
    Fail "营级同输入两次运行事件序列不一致"
}

# script-req-assign：提交 → 评估 → 配属 → 归建 → 归还。
Assert-Sequence $bnEvents @(
    '^SUPPORT_REQUESTED request=script-req-assign ',
    '^SUPPORT_EVALUATING request=script-req-assign ',
    '^SUPPORT_ASSIGNED request=script-req-assign ',
    '^ATTACH_RETURNING request=script-req-assign ',
    '^ATTACH_RETURNED request=script-req-assign '
) '营级配属与归建序列'

# script-req-reject：范围外请求明确拒绝（SCOPE_VIOLATION），不产生配属事件。
Assert-Sequence $bnEvents @(
    '^SUPPORT_REQUESTED request=script-req-reject ',
    '^SUPPORT_EVALUATING request=script-req-reject ',
    '^SUPPORT_REJECTED request=script-req-reject '
) '营级拒绝序列'
$bnReject = Assert-Match $bnEvents '^SUPPORT_REJECTED request=script-req-reject ' '营级拒绝事件'
if ($bnReject[0].Message -notmatch 'SCOPE_VIOLATION') {
    Fail "营级拒绝原因应为 SCOPE_VIOLATION: $($bnReject[0].Message)"
}
if ((Match-Events $bnEvents '^SUPPORT_ASSIGNED request=script-req-reject ').Count -ne 0) {
    Fail "范围外请求不得产生配属事件"
}

# script-req-escalate：可用力量不足 → 向上转请（brigade）→ 无更上级 → 明确拒绝。
Assert-Sequence $bnEvents @(
    '^SUPPORT_REQUESTED request=script-req-escalate ',
    '^SUPPORT_EVALUATING request=script-req-escalate ',
    '^SUPPORT_ESCALATED request=script-req-escalate ',
    '^SUPPORT_REJECTED request=script-req-escalate '
) '营级转请后拒绝序列'
$escalated = Assert-Match $bnEvents '^SUPPORT_ESCALATED request=script-req-escalate ' '营级转请事件'
if ($escalated[0].Message -notmatch 'to_node=node-brigade-1') {
    Fail "转请目标必须为旅级节点: $($escalated[0].Message)"
}
$bnEscalatedReject = Assert-Match $bnEvents '^SUPPORT_REJECTED request=script-req-escalate ' '营级转请后拒绝'
if ($bnEscalatedReject[0].Message -notmatch 'NO_SUPERIOR_ESCALATION') {
    Fail "转请后拒绝原因应为 NO_SUPERIOR_ESCALATION: $($bnEscalatedReject[0].Message)"
}

Remove-Item -LiteralPath $tempDir -Recurse -Force
Write-Host 'SUPPORT CHAIN TESTS PASSED'
exit 0
