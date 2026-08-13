# tests/cli_tests/run_faction_chain_tests.ps1
#
# T054 派系差异观测集成测试 runner（宪法 15：无头可测；宪法 7：确定性）。
#
# 覆盖（tasks.md T054；SC-004；FR-006/008）：
# - 同一连排级请求脚本在三派系（中国/北约/苏俄）场景分别运行，可观察差异
#   指标真实存在且互不相同：
#   * 审批层级基线 approval_level 0/1/2 → SUPPORT_EVALUATING 携带
#     approval_level/approval_delay（0/10/20 tick），resolve_tick 与
#     evaluating_tick 之差 = 基础评估延迟 20 + 审批转发延迟；
#   * 有限分数总额与成本不同（60/55/45、迫击炮 30/30/40；中国另有专属
#     hmg 15）→ 全部配属扣分合计 45/30/40、最终 score_remaining 15/25/5
#     互不相同（按池总额 − 扣分合计断言，与请求裁决顺序无关）；
#   * 可用支援种类/数量集合不同 → 中国专属 squad-hmg-team 配属成功，
#     北约/苏俄 SCOPE_VIOLATION；苏俄 atgm 池数量 1 < 请求 2 →
#     INSUFFICIENT_AVAILABLE，而中国/北约为 INSUFFICIENT_SCORE；
# - 同一派系同输入两次运行状态哈希与事件序列一致（确定性回归）；
# - 三派系运行哈希两两不同（差异指标落进确定性状态）。
#
# 所有断言失败以非零退出码结束（宪法 17：不静默吞错）。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$ScenarioChina,
    [Parameter(Mandatory = $true)][string]$ScenarioNato,
    [Parameter(Mandatory = $true)][string]$ScenarioRussia,
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Seed = "7",
    [string]$Ticks = "1200"
)

$ErrorActionPreference = 'Stop'
$tempDir = Join-Path $env:TEMP ("wfs-faction-chain-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

function Fail([string]$Message) {
    Write-Host "FACTION CHAIN TEST FAILED: $Message"
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

# 三派系配置表：场景、审批层级基线、审批转发延迟（场景 approval_step_ticks=10）、
# 各派系池总额/迫击炮成本与最终剩余（池总额 − 全部配属扣分：
# 60−30−15=15、55−30=25、45−40=5）、cmd-2/cmd-3 预期。
$factions = @(
    @{
        Id = 'faction-china'
        Scenario = $ScenarioChina
        ApprovalLevel = 0
        ApprovalDelay = 0
        PoolTotal = 60
        MortarCost = 30
        FinalRemaining = 15
        Cmd2Reason = 'INSUFFICIENT_SCORE'
        Cmd3Assigned = $true
    },
    @{
        Id = 'faction-nato'
        Scenario = $ScenarioNato
        ApprovalLevel = 1
        ApprovalDelay = 10
        PoolTotal = 55
        MortarCost = 30
        FinalRemaining = 25
        Cmd2Reason = 'INSUFFICIENT_SCORE'
        Cmd3Assigned = $false
    },
    @{
        Id = 'faction-russia'
        Scenario = $ScenarioRussia
        ApprovalLevel = 2
        ApprovalDelay = 20
        PoolTotal = 45
        MortarCost = 40
        FinalRemaining = 5
        Cmd2Reason = 'INSUFFICIENT_AVAILABLE'
        Cmd3Assigned = $false
    }
)

$results = @{}
foreach ($faction in $factions) {
    $jsonl = Join-Path $tempDir ($faction.Id + '.jsonl')
    $r = Invoke-Cli @('inject', '--scenario', $faction.Scenario, '--seed', $Seed, '--threads', '1', '--ticks', $Ticks,
        '--script', $Script, '--out', $jsonl, '--hash')
    if ($r.Code -ne 0) {
        Fail "$($faction.Id) inject 退出码 $($r.Code): $($r.Output)"
    }
    $hashFirst = Last-Line $r.Output
    $events = Read-Events $jsonl

    $jsonl2 = Join-Path $tempDir ($faction.Id + '2.jsonl')
    $r2 = Invoke-Cli @('inject', '--scenario', $faction.Scenario, '--seed', $Seed, '--threads', '2', '--ticks', $Ticks,
        '--script', $Script, '--out', $jsonl2, '--hash')
    if ($r2.Code -ne 0) {
        Fail "$($faction.Id) inject 第二次运行退出码 $($r2.Code): $($r2.Output)"
    }
    $hashSecond = Last-Line $r2.Output
    $events2 = Read-Events $jsonl2
    if ($hashFirst -ne $hashSecond) {
        Fail "$($faction.Id) 同输入两次运行状态哈希不一致"
    }
    $messagesFirst = @($events | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
    $messagesSecond = @($events2 | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
    if (($messagesFirst -join "`n") -ne ($messagesSecond -join "`n")) {
        Fail "$($faction.Id) 同输入两次运行事件序列不一致"
    }

    # 三条支援请求 + 目标任务都必须下达。
    foreach ($cmdId in @('cmd-0', 'cmd-1', 'cmd-2', 'cmd-3')) {
        Assert-Match $events "^COMMAND_ISSUED command=$($cmdId) " "下达事件 $cmdId"
    }

    # 派系/审批层级可观察字段：faction、approval_level、approval_delay，
    # 且 resolve_tick = evaluating_tick + 20 + approval_delay（场景基础延迟 20）。
    $evaluating = Assert-Match $events '^SUPPORT_EVALUATING request=req-cmd-1 ' "$($faction.Id) 评估事件"
    $evalMessage = $evaluating[0].Message
    if ($evalMessage -notmatch ("faction=" + $faction.Id)) {
        Fail "$($faction.Id) 评估事件缺少 faction 字段: $evalMessage"
    }
    if ($evalMessage -notmatch ("approval_level=" + $faction.ApprovalLevel)) {
        Fail "$($faction.Id) 审批层级字段错误（应为 $($faction.ApprovalLevel)）: $evalMessage"
    }
    if ($evalMessage -notmatch ("approval_delay=" + $faction.ApprovalDelay)) {
        Fail "$($faction.Id) 审批转发延迟字段错误（应为 $($faction.ApprovalDelay)）: $evalMessage"
    }
    $evalTick = [long](Extract $evalMessage 'evaluating_tick=(\d+)')
    $resolveTick = [long](Extract $evalMessage 'resolve_tick=(\d+)')
    if (($resolveTick - $evalTick) -ne (20 + [long]$faction.ApprovalDelay)) {
        Fail "$($faction.Id) 评估延迟错误：resolve-eval=$($resolveTick - $evalTick)（应 20+$($faction.ApprovalDelay)）: $evalMessage"
    }

    # cmd-1 三派系均成功：扣分=迫击炮成本（30/30/40）；最终剩余另按
    # 全部配属扣分合计断言（与裁决顺序无关）。
    Assert-Sequence $events @(
        '^SUPPORT_REQUESTED request=req-cmd-1 ',
        '^SUPPORT_EVALUATING request=req-cmd-1 ',
        '^SUPPORT_ASSIGNED request=req-cmd-1 '
    ) "$($faction.Id) 成功请求"
    $assigned1 = Assert-Match $events '^SUPPORT_ASSIGNED request=req-cmd-1 ' "$($faction.Id) 配属事件"
    if ($assigned1[0].Message -notmatch ("score_cost=" + $faction.MortarCost)) {
        Fail "$($faction.Id) 迫击炮扣分错误（应 $($faction.MortarCost)）: $($assigned1[0].Message)"
    }
    if ($assigned1[0].Message -notmatch 'units=\[squad-mortar-team\]') {
        Fail "$($faction.Id) 配属单位清单错误: $($assigned1[0].Message)"
    }

    # cmd-2 拒绝：中国/北约分数不足，苏俄池数量不足（拒绝原因可观察差异）。
    Assert-Sequence $events @(
        '^SUPPORT_REQUESTED request=req-cmd-2 ',
        '^SUPPORT_EVALUATING request=req-cmd-2 ',
        '^SUPPORT_REJECTED request=req-cmd-2 '
    ) "$($faction.Id) 拒绝请求"
    $rejected2 = Assert-Match $events '^SUPPORT_REJECTED request=req-cmd-2 ' "$($faction.Id) 拒绝事件"
    if ($rejected2[0].Message -notmatch ($faction.Cmd2Reason)) {
        Fail "$($faction.Id) cmd-2 拒绝原因应为 $($faction.Cmd2Reason): $($rejected2[0].Message)"
    }

    # cmd-3 池种类差异：中国专属 squad-hmg-team 可配属；北约/苏俄不在池内拒绝。
    if ($faction.Cmd3Assigned) {
        Assert-Sequence $events @(
            '^SUPPORT_REQUESTED request=req-cmd-3 ',
            '^SUPPORT_EVALUATING request=req-cmd-3 ',
            '^SUPPORT_ASSIGNED request=req-cmd-3 '
        ) "$($faction.Id) 池专属种类配属"
        $assigned3 = Assert-Match $events '^SUPPORT_ASSIGNED request=req-cmd-3 ' "$($faction.Id) 池专属配属事件"
        if ($assigned3[0].Message -notmatch 'units=\[squad-hmg-team\]' -or
            $assigned3[0].Message -notmatch 'score_cost=15') {
            Fail "$($faction.Id) cmd-3 配属字段错误（units=[squad-hmg-team] cost=15）: $($assigned3[0].Message)"
        }
    } else {
        Assert-Sequence $events @(
            '^SUPPORT_REQUESTED request=req-cmd-3 ',
            '^SUPPORT_EVALUATING request=req-cmd-3 ',
            '^SUPPORT_REJECTED request=req-cmd-3 '
        ) "$($faction.Id) 池外种类拒绝"
        $rejected3 = Assert-Match $events '^SUPPORT_REJECTED request=req-cmd-3 ' "$($faction.Id) 池外种类拒绝事件"
        if ($rejected3[0].Message -notmatch 'SCOPE_VIOLATION' -or
            $rejected3[0].Message -notmatch 'squad-hmg-team') {
            Fail "$($faction.Id) cmd-3 应因 squad-hmg-team 不在池内 SCOPE_VIOLATION 拒绝: $($rejected3[0].Message)"
        }
        if ((Match-Events $events '^SUPPORT_ASSIGNED request=req-cmd-3 ').Count -ne 0) {
            Fail "$($faction.Id) 池外种类不得产生配属事件"
        }
    }

    # 最终剩余分数：池总额 − 全部 SUPPORT_ASSIGNED 扣分合计，必须等于派系
    # 预期值 15/25/5——对请求裁决顺序不敏感，仍逐派系互不相同。
    $totalConsumed = 0
    foreach ($assigned in (Match-Events $events '^SUPPORT_ASSIGNED ')) {
        $totalConsumed += [long](Extract $assigned.Message 'score_cost=(\d+)')
    }
    $finalRemaining = [long]$faction.PoolTotal - $totalConsumed
    if ($finalRemaining -ne [long]$faction.FinalRemaining) {
        Fail "$($faction.Id) 最终剩余分数错误：池总额 $($faction.PoolTotal) − 扣分合计 $totalConsumed = $finalRemaining（应 $($faction.FinalRemaining)）"
    }

    # 目标任务完成 → 支援归建（SC-004 闭环在三派系均成立）。
    Assert-Sequence $events @(
        '^SUPPORT_ASSIGNED request=req-cmd-1 ',
        'MISSION_COMPLETED unit=sp-squad-1',
        '^ATTACH_RETURNING request=req-cmd-1 ',
        '^ATTACH_RETURNED request=req-cmd-1 '
    ) "$($faction.Id) 归建序列"

    $results[$faction.Id] = [pscustomobject]@{
        Hash = $hashFirst
        ApprovalLevel = [long]$faction.ApprovalLevel
        ApprovalDelay = [long]$faction.ApprovalDelay
        FinalRemaining = [long]$faction.FinalRemaining
    }
}

# 跨派系差异断言：审批层级 0/1/2、转发延迟 0/10/20、最终剩余 15/25/5 两两不同，
# 三派系状态哈希互不相同（差异落进确定性状态）。
$approvalLevels = @($results.Values | ForEach-Object { $_.ApprovalLevel } | Sort-Object -Unique)
if (($approvalLevels -join ',') -ne '0,1,2') {
    Fail "审批层级基线未覆盖 0/1/2：$($approvalLevels -join ',')"
}
$approvalDelays = @($results.Values | ForEach-Object { $_.ApprovalDelay } | Sort-Object -Unique)
if (($approvalDelays -join ',') -ne '0,10,20') {
    Fail "审批转发延迟未覆盖 0/10/20：$($approvalDelays -join ',')"
}
$remainings = @($results.Values | ForEach-Object { $_.FinalRemaining } | Sort-Object -Unique)
if (($remainings -join ',') -ne '5,15,25') {
    Fail "最终剩余分数未覆盖 5/15/25：$($remainings -join ',')"
}
$hashes = @($results.Values | ForEach-Object { $_.Hash } | Sort-Object -Unique)
if ($hashes.Count -ne 3) {
    Fail "三派系状态哈希必须两两不同（实际 $($hashes.Count) 个唯一哈希）"
}

Remove-Item -LiteralPath $tempDir -Recurse -Force
Write-Host 'FACTION CHAIN TESTS PASSED'
exit 0
