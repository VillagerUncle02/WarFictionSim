# tests/cli_tests/run_intel_roles_tests.ps1
#
# T055 情报权限验收测试 runner（FR-028/031/051，SC-005；宪法 7/15/17）。
#
# 覆盖（tasks.md T055；quickstart §3.11）：
# - 上级仅见摘要：SUMMARY_REPORT 只含聚合字段（任务状态/完成度/损失/支援），
#   不含下级内部细节（单位 id/坐标/武器）；
# - 下级完整状态：连排级单位的命令/任务事件携带完整单位细节；
# - 摘要快照语义：不同生成时刻的两份摘要各自冻结对应时刻的数值
#   （生成时刻后状态变化不改写已上报摘要）；
# - 来源标注：INTEL_OBSERVED 直属来源（具体单位/节点）、INTEL_RELAY
#   下级上报（sync，标注发现单位）与更上级转发（relay，只标层级）；
# - 层级同步间隔：连排级 5s（100 tick）、营级 15s（300 tick）；
# - 同输入两次运行（threads 1 vs 2）状态哈希与事件序列一致。
#
# 所有断言失败以非零退出码结束（宪法 17：不静默吞错）。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Seed = "7",
    [string]$Ticks = "1200"
)

$ErrorActionPreference = 'Stop'
$tempDir = Join-Path $env:TEMP ("wfs-intel-roles-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

function Fail([string]$Message) {
    Write-Host "INTEL ROLES TEST FAILED: $Message"
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

# ---- 确定性基线：同输入两次运行哈希与事件序列一致 ---- 
$jsonl = Join-Path $tempDir 'intel-roles.jsonl'
$r = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', $Seed, '--threads', '1', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl, '--hash')
if ($r.Code -ne 0) {
    Fail "inject 退出码 $($r.Code): $($r.Output)"
}
$hashFirst = Last-Line $r.Output
$events = Read-Events $jsonl

$jsonl2 = Join-Path $tempDir 'intel-roles-2.jsonl'
$r2 = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', $Seed, '--threads', '2', '--ticks', $Ticks,
    '--script', $Script, '--out', $jsonl2, '--hash')
if ($r2.Code -ne 0) {
    Fail "inject 第二次运行退出码 $($r2.Code): $($r2.Output)"
}
$hashSecond = Last-Line $r2.Output
$events2 = Read-Events $jsonl2
if ($hashFirst -ne $hashSecond) {
    Fail "同输入两次运行状态哈希不一致"
}
$messagesFirst = @($events | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
$messagesSecond = @($events2 | ForEach-Object { "$($_.Seq):$($_.Tick):$($_.Message)" })
if (($messagesFirst -join "`n") -ne ($messagesSecond -join "`n")) {
    Fail "同输入两次运行事件序列不一致"
}

# 两条命令均下达（下级完整状态的入口）。
foreach ($cmdId in @('cmd-0', 'cmd-1')) {
    Assert-Match $events "^COMMAND_ISSUED command=$($cmdId) " "下达事件 $cmdId"
}

# 下级完整状态：连排级单位命令确认/生效/完成事件携带完整单位细节。
Assert-Match $events '^UNIT_MOVING unit=plt-1-sq-1 ' '连排级单位移动事件（完整状态）'
$completed = Assert-Match $events '^MISSION_COMPLETED unit=plt-1-sq-1 ' '连排级单位任务完成事件（完整状态）'
if ($completed[0].Message -notmatch 'type=MOVE') {
    Fail "任务完成事件必须携带任务类型（下级完整状态）: $($completed[0].Message)"
}

# 来源标注（SC-005/FR-031）：直属发现标注具体单位与节点，100% 可见。
$observed = Assert-Match $events '^INTEL_OBSERVED observer=plt-1-sq-1 target=enemy-sq-1 ' '直属发现事件'
if ($observed[0].Message -notmatch 'source=direct') {
    Fail "直属发现必须标注 source=direct: $($observed[0].Message)"
}
if ($observed[0].Message -notmatch 'source_unit=plt-1-sq-1' -or $observed[0].Message -notmatch 'source_node=node-plt-1') {
    Fail "直属发现必须标注具体来源单位与节点: $($observed[0].Message)"
}
if ($observed[0].Message -notmatch 'tier=T') {
    Fail "直属发现必须携带识别档位: $($observed[0].Message)"
}

# 层级化同步间隔（FR-028）：连排级 100 tick（5s）、营级 300 tick（15s）。
$companySync = Assert-Match $events '^INTEL_SYNC node=node-co-1 level=company tick=100 ' '连排级首次层级同步（tick=100）'
if ($companySync[0].Message -notmatch 'from=node-plt-1') {
    Fail "连排级同步必须标注来源子节点: $($companySync[0].Message)"
}
Assert-Match $events '^INTEL_SYNC node=node-co-1 level=company tick=200 ' '连排级第二次层级同步（tick=200）'
Assert-Match $events '^INTEL_SYNC node=node-bn-1 level=battalion tick=300 ' '营级首次层级同步（tick=300）'
Assert-Match $events '^INTEL_SYNC node=node-bn-1 level=battalion tick=600 ' '营级第二次层级同步（tick=600）'

# 情报来源标注：下级上报标注具体发现单位；更上级转发只标层级。
$relayCompany = Assert-Match $events '^INTEL_RELAY to=node-co-1 target=enemy-sq-1 ' '连级情报转送'
if ($relayCompany[0].Message -notmatch 'source_kind=sync') {
    Fail "下级上报的来源标注应为 sync: $($relayCompany[0].Message)"
}
if ($relayCompany[0].Message -notmatch 'source_unit=plt-1-sq-1' -or $relayCompany[0].Message -notmatch 'source_node=node-plt-1') {
    Fail "下级上报必须标注具体发现单位与节点: $($relayCompany[0].Message)"
}
$relayBattalion = Assert-Match $events '^INTEL_RELAY to=node-bn-1 target=enemy-sq-1 ' '营级情报转发'
if ($relayBattalion[0].Message -notmatch 'source_kind=relay') {
    Fail "更上级转发的来源标注应为 relay: $($relayBattalion[0].Message)"
}
if ($relayBattalion[0].Message -notmatch 'source_level=company') {
    Fail "更上级转发只标注来源层级（company）: $($relayBattalion[0].Message)"
}

# 上级仅见摘要（FR-051）：摘要只含聚合字段，不含下级内部细节。
$firstSummary = Assert-Match $events '^SUMMARY_REPORT interaction=SUMMARY_REPORT from=node-plt-1 to=node-co-1 generated_tick=100 ' '首份摘要（tick=100）'
if ($firstSummary[0].Message -notmatch 'missions=' -or $firstSummary[0].Message -notmatch 'completion_pct=') {
    Fail "摘要必须携带任务状态与完成度: $($firstSummary[0].Message)"
}
if ($firstSummary[0].Message -notmatch 'losses=' -or $firstSummary[0].Message -notmatch 'support_requests=') {
    Fail "摘要必须携带损失摘要与支援需求: $($firstSummary[0].Message)"
}
if ($firstSummary[0].Message -match '\bunit=' -or $firstSummary[0].Message -match '\bx=[0-9.]+\b' -or
    $firstSummary[0].Message -match '\bweapon=') {
    Fail "上级摘要不得包含下级内部细节（单位/坐标/武器）: $($firstSummary[0].Message)"
}

# 摘要快照语义（CHK165/FR-051）：tick=100 摘要冻结 active>=1/completed=0，
# 后续状态变化不改写；更晚的摘要各自携带生成时刻的数值（completed>=1）。
$lateSummary = Assert-Match $events '^SUMMARY_REPORT interaction=SUMMARY_REPORT from=node-plt-1 to=node-co-1 generated_tick=600 ' '晚生成摘要（tick=600）'
if ($firstSummary[0].Message -notmatch 'completed=0') {
    Fail "tick=100 摘要必须冻结生成时刻的 completed=0: $($firstSummary[0].Message)"
}
if ($lateSummary[0].Message -notmatch 'completed=[1-9]') {
    Fail "tick=600 摘要必须反映生成时刻的已完成任务: $($lateSummary[0].Message)"
}

# 营级玩家（更上级）同样只接收下级摘要：营→连摘要存在且无内部细节。
$battalionSummary = Assert-Match $events '^SUMMARY_REPORT interaction=SUMMARY_REPORT from=node-co-1 to=node-bn-1 ' '连→营摘要'
if ($battalionSummary[0].Message -match '\bunit=plt-1-sq-1\b' -or $battalionSummary[0].Message -match '\bx=[0-9.]+\b') {
    Fail "营级接收的摘要不得透传排级内部细节: $($battalionSummary[0].Message)"
}

Remove-Item -LiteralPath $tempDir -Recurse -Force
Write-Host 'INTEL ROLES TESTS PASSED'
exit 0
