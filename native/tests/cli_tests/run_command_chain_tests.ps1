# tests/cli_tests/run_command_chain_tests.ps1
#
# T023 命令链路集成测试 runner（宪法 15：无头可测；宪法 7：确定性）。
#
# 覆盖（tasks.md T023；quickstart §3.2；FR-030/040/043/045/046）：
# - 下达→确认接受→移动→完成 的完整事件序列
#   （COMMAND_ISSUED → COMMAND_ACKNOWLEDGED → UNIT_MOVING → MISSION_COMPLETED）；
# - 连排级 3–10s（20 Hz 下 60–200 tick）通讯延迟时间戳比对；
# - 生效前修改（MODIFY_COMMAND 高优先级取代原命令）；
# - 生效前撤回（WITHDRAW_COMMAND 高优先级使原命令永不生效）；
# - （优先级, 序列号）裁决：同 tick 同优先级按到达序列号先到者生效；
# - 批量部分接受（BATCH_PARTIAL_ACCEPT：满足条件的单位接受、不满足的拒绝）。
# - 同输入两次运行哈希与事件序列一致（确定性回归）。
#
# 所有断言失败以非零退出码结束（宪法 17：不静默吞错）。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Seed = "2",
    [string]$Ticks = "1200"
)

$ErrorActionPreference = 'Stop'
$tempDir = Join-Path $env:TEMP ("wfs-command-chain-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

function Fail([string]$Message) {
    Write-Host "COMMAND CHAIN TEST FAILED: $Message"
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

function Extract([string]$Message, [string]$Pattern) {
    if ($Message -notmatch $Pattern) { return "" }
    return $Matches[1]
}

$jsonl = Join-Path $tempDir 'chain.jsonl'
$r = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', $Seed, '--threads', '1', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl, '--hash')
if ($r.Code -ne 0) {
    Fail "inject 退出码 $($r.Code): $($r.Output)"
}
$hashFirst = Last-Line $r.Output
$events = Read-Events $jsonl

# 第二次运行：同一输入必须产生同一哈希与同一事件序列（确定性，宪法 7）。
$jsonl2 = Join-Path $tempDir 'chain2.jsonl'
$r2 = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', $Seed, '--threads', '2', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl2, '--hash')
if ($r2.Code -ne 0) {
    Fail "inject 第二次运行退出码 $($r2.Code): $($r2.Output)"
}
$hashSecond = Last-Line $r2.Output
$events2 = Read-Events $jsonl2
if ($hashFirst -ne $hashSecond) {
    Fail "同输入两次运行状态哈希不一致: $hashFirst vs $hashSecond"
}
$messagesFirst = @($events | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
$messagesSecond = @($events2 | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
if (($messagesFirst -join "`n") -ne ($messagesSecond -join "`n")) {
    Fail "同输入两次运行事件序列不一致（线程数不影响确定性）"
}

# 1) 下达事件：7 条命令全部产生 COMMAND_ISSUED，含命令号/单位/延迟信息。
$issued = Assert-Match $events '^COMMAND_ISSUED ' '下达事件'
if ($issued.Count -lt 7) {
    Fail "COMMAND_ISSUED 数量不足: $($issued.Count)（期望 >= 7）"
}

# 2) 通讯延迟：任务命令 ack_tick - issue_tick 必须落在连排级 3–10s
#    （20 Hz 下 60–200 tick）区间内（FR-030/SC-003）。
foreach ($cmdId in @('cmd-0', 'cmd-1', 'cmd-2', 'cmd-3', 'cmd-4', 'cmd-5', 'cmd-6')) {
    $issue = Match-Events $events "^COMMAND_ISSUED command=$($cmdId) "
    $ack = Match-Events $events "^COMMAND_ACKNOWLEDGED command=$($cmdId) "
    if ($issue.Count -ne 1) {
        Fail "缺少唯一下达事件 command=$cmdId"
    }
    $issueTick = [long](Extract $issue[0].Message 'issue_tick=(\d+)')
    if ($ack.Count -eq 0) {
        # 被同优先级先到者裁决拒绝的命令不产生确认（cmd-3 预期路径）。
        continue
    }
    $ackTick = [long](Extract $ack[0].Message 'arrival_tick=(\d+)')
    $delay = $ackTick - $issueTick
    if ($delay -lt 60 -or $delay -gt 200) {
        Fail "command=$cmdId 通讯延迟 $delay tick 不在连排级 3–10s（60–200 tick）区间"
    }
}

# 3) squad-1：生效前修改（MODIFY_COMMAND 高优先级取代原 MOVE）。
#    事件序列：确认 → 移动（p2）→ 完成。
$s1Ack = Assert-Match $events '^COMMAND_ACKNOWLEDGED command=cmd-1 ' 'squad-1 修改命令确认'
$s1Moving = Assert-Match $events 'UNIT_MOVING unit=tutorial-squad-1 target=\(0\.41[0-9]*,0\.34[0-9]*\)' 'squad-1 移动到 p2'
$s1Done = Assert-Match $events 'MISSION_COMPLETED unit=tutorial-squad-1' 'squad-1 任务完成'
if ($s1Ack[0].Seq -ge $s1Moving[0].Seq -or $s1Moving[0].Seq -ge $s1Done[0].Seq) {
    Fail "squad-1 事件顺序错误（期望 确认 < 移动 < 完成）"
}
$superseded = Assert-Match $events 'COMMAND_SUPERSEDED command=cmd-0 ' 'squad-1 原命令被取代'
if ((Match-Events $events 'UNIT_MOVING unit=tutorial-squad-1 target=\(0\.38[0-9]*,0\.33[0-9]*\)').Count -ne 0) {
    Fail "squad-1 不应移动到被取代的原目标 p1"
}

# 4) squad-2：（优先级, 序列号）裁决——同 tick 同优先级先到者（seq 更小）生效，
#    后到的撤回命令被拒绝（COMMAND_REJECTED），原移动任务正常完成。
$s2Rejected = Assert-Match $events 'COMMAND_REJECTED command=cmd-3 ' 'squad-2 撤回命令被裁决拒绝'
$s2Ack = Assert-Match $events '^COMMAND_ACKNOWLEDGED command=cmd-2 ' 'squad-2 移动命令确认'
$s2Moving = Assert-Match $events 'UNIT_MOVING unit=tutorial-squad-2 target=\(0\.43[0-9]*,0\.47[0-9]*\)' 'squad-2 移动到 p3'
$s2Done = Assert-Match $events 'MISSION_COMPLETED unit=tutorial-squad-2' 'squad-2 任务完成'
if ($s2Ack[0].Seq -ge $s2Moving[0].Seq -or $s2Moving[0].Seq -ge $s2Done[0].Seq) {
    Fail "squad-2 事件顺序错误（期望 确认 < 移动 < 完成）"
}

# 5) squad-3：高优先级生效前撤回——原 MOVE 永不生效（无该目标的移动事件），
#    撤回命令产生 COMMAND_WITHDRAWN。
$s3Withdrawn = Assert-Match $events 'COMMAND_WITHDRAWN command=cmd-4 ' 'squad-3 原命令被撤回'
if ((Match-Events $events 'UNIT_MOVING unit=tutorial-squad-3 target=\(0\.53[0-9]*,0\.38[0-9]*\)').Count -ne 0) {
    Fail "squad-3 不应移动到被撤回的目标 p4"
}

# 6) 批量部分接受：squad-3 接受（弹药匹配）、squad-2 拒绝（缺 ammo-frag-grenade），
#    批量有效单位继续执行并完成。
$partial = Assert-Match $events 'BATCH_PARTIAL_ACCEPT command=cmd-6 ' '批量部分接受事件'
$partialMessage = $partial[0].Message
if ($partialMessage -notmatch 'accepted=\[tutorial-squad-3\]' -or $partialMessage -notmatch 'rejected=\[tutorial-squad-2\]') {
    Fail "批量部分接受结果错误: $partialMessage"
}
$batchReject = Assert-Match $events 'COMMAND_REJECTED command=cmd-6 unit=tutorial-squad-2 ' '批量单位拒绝'
if ($batchReject[0].Message -notmatch 'AMMO_NOT_FOUND') {
    Fail "批量单位拒绝原因应为弹药不匹配: $($batchReject[0].Message)"
}
$s3BatchMoving = Assert-Match $events 'UNIT_MOVING unit=tutorial-squad-3 target=\(0\.55[0-9]*,0\.4[0-9]*\)' 'squad-3 批量移动'
$s3BatchDone = Assert-Match $events 'MISSION_COMPLETED unit=tutorial-squad-3' 'squad-3 批量任务完成'
if ($s3BatchMoving[0].Seq -ge $s3BatchDone[0].Seq) {
    Fail "squad-3 批量移动/完成事件顺序错误"
}

Remove-Item -LiteralPath $tempDir -Recurse -Force
Write-Host 'COMMAND CHAIN TESTS PASSED'
exit 0
