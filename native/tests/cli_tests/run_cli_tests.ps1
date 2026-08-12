# tests/cli_tests/run_cli_tests.ps1
#
# T019 无头 CLI 集成测试（宪法 15：无头可测；宪法 17：错误显式报错）。
# 由 CTest 调用：驱动 sim_headless.exe 覆盖 run/inject/save 子命令、参数解析、
# --hash 输出、JSONL 事件输出与错误路径；所有断言失败以非零退出码结束。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Script
)

$ErrorActionPreference = 'Stop'
$tempDir = Join-Path $env:TEMP ("wfs-cli-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

function Fail([string]$Message) {
    Write-Host "CLI TEST FAILED: $Message"
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

function Read-Jsonl([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Fail "缺少 JSONL 输出文件: $Path"
    }
    $messages = @()
    $lineIndex = 0
    $prevSeq = -1
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line.Trim().Length -eq 0) { continue }
        try {
            $obj = $line | ConvertFrom-Json
        } catch {
            Fail "JSONL 第 $lineIndex 行不是合法 JSON: $line"
        }
        foreach ($key in @('seq', 'tick', 'category', 'severity', 'message')) {
            if ($null -eq $obj.$key) {
                Fail "JSONL 第 $lineIndex 行缺少字段 ${key}: $line"
            }
        }
        $seq = [long]$obj.seq
        if ($seq -le $prevSeq) {
            Fail "JSONL 第 $lineIndex 行 seq 非严格升序: $seq（前序 $prevSeq）"
        }
        $prevSeq = $seq
        $messages += [string]$obj.message
        $lineIndex++
    }
    if ($messages.Count -eq 0) {
        Fail "JSONL 输出为空: $Path"
    }
    return $messages
}

# 1. run --hash：0 退出码 + 64 位小写十六进制哈希。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--threads', '1', '--ticks', '20', '--hash')
if ($r.Code -ne 0) { Fail "run --hash 退出码 $($r.Code): $($r.Output)" }
if ((Last-Line $r.Output) -notmatch '^[0-9a-f]{64}$') {
    Fail "run --hash 输出不是 64 位十六进制: $($r.Output)"
}

# 2. run --ai-backend script --out：JSONL 事件输出包含 AI 接受与命令处理事件。
$jsonl = Join-Path $tempDir 'run-script.jsonl'
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--threads', '2', '--ticks', '20',
    '--ai-backend', 'script', '--out', $jsonl)
if ($r.Code -ne 0) { Fail "run --ai-backend script 退出码 $($r.Code): $($r.Output)" }
$messages = Read-Jsonl $jsonl
if (-not ($messages -match 'AI_DECISION_ACCEPTED')) { Fail "JSONL 缺少 AI_DECISION_ACCEPTED 事件" }
if (-not ($messages -match 'COMMAND_PROCESSED')) { Fail "JSONL 缺少 COMMAND_PROCESSED 事件" }

# 3. run --ai-backend cloud：未实现必须显式报错（宪法 17，不允许静默降级）。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--ai-backend', 'cloud')
if ($r.Code -eq 0) { Fail "cloud 后端应报未实现错误，却成功退出" }

# 4. 未知参数与非法线程数：参数解析错误路径必须失败。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--bogus')
if ($r.Code -eq 0) { Fail "未知参数应失败" }
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--threads', '0')
if ($r.Code -eq 0) { Fail "--threads 0 应失败" }

# 5. save：WFS-SAVE magic + 0 退出码。
$save = Join-Path $tempDir 'save.wfs'
$r = Invoke-Cli @('save', '--scenario', $Scenario, '--seed', '42', '--out', $save)
if ($r.Code -ne 0) { Fail "save 退出码 $($r.Code): $($r.Output)" }
if (-not (Test-Path -LiteralPath $save)) { Fail "save 未生成存档: $save" }
$bytes = [System.IO.File]::ReadAllBytes($save)
$magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(8, $bytes.Length))
if ($magic -ne 'WFS-SAVE') { Fail "存档 magic 错误: $magic" }

# 6. inject --script：玩家命令入队并处理（COMMAND_QUEUED / COMMAND_PROCESSED）。
$injectOut = Join-Path $tempDir 'inject.jsonl'
$r = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', '42', '--script', $Script, '--ticks', '20',
    '--out', $injectOut)
if ($r.Code -ne 0) { Fail "inject 退出码 $($r.Code): $($r.Output)" }
$injectMessages = Read-Jsonl $injectOut
if (-not ($injectMessages -match 'COMMAND_QUEUED')) { Fail "JSONL 缺少 COMMAND_QUEUED 事件" }
if (-not ($injectMessages -match 'COMMAND_PROCESSED')) { Fail "JSONL 缺少 COMMAND_PROCESSED 事件" }

# 7. 错误路径：缺 --script、缺子命令、场景不存在。
$r = Invoke-Cli @('inject', '--scenario', $Scenario, '--seed', '42')
if ($r.Code -eq 0) { Fail "inject 缺 --script 应失败" }
$r = Invoke-Cli @()
if ($r.Code -eq 0) { Fail "缺少子命令应失败" }
$r = Invoke-Cli @('run', '--scenario', (Join-Path $tempDir 'missing.json'), '--seed', '42')
if ($r.Code -eq 0) { Fail "场景不存在应失败" }

# 8. 负数/非法数值：--ticks -1 与 --seed -1 必须拒绝（stoull 回绕防护）。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--ticks', '-1')
if ($r.Code -eq 0) { Fail "--ticks -1 应失败" }
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '-1')
if ($r.Code -eq 0) { Fail "--seed -1 应失败" }

# 9. 子命令旗标不匹配：run --script / inject --ai-backend / save --hash|--ticks
#    必须显式报错而非静默忽略（宪法 17）。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--script', $Script)
if ($r.Code -eq 0) { Fail "run 不支持 --script 应失败" }
$r = Invoke-Cli @('inject', '--scenario', $Scenario, '--script', $Script, '--ai-backend', 'script')
if ($r.Code -eq 0) { Fail "inject 不支持 --ai-backend 应失败" }
$r = Invoke-Cli @('save', '--scenario', $Scenario, '--out', (Join-Path $tempDir 'flag.wfs'), '--hash')
if ($r.Code -eq 0) { Fail "save 不支持 --hash 应失败" }
$r = Invoke-Cli @('save', '--scenario', $Scenario, '--out', (Join-Path $tempDir 'flag2.wfs'), '--ticks', '10')
if ($r.Code -eq 0) { Fail "save 不支持 --ticks 应失败" }

# 10. --threads 非法值：-1/+1 必须在参数解析层拒绝（用法错误退出码 2），
#     而不是拖到驱动层才失败（与 --ticks -1 的 ParseUint64 校验对称）。
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--threads', '-1')
if ($r.Code -ne 2) { Fail "--threads -1 应以用法错误退出码 2 失败（实际 $($r.Code)）" }
$r = Invoke-Cli @('run', '--scenario', $Scenario, '--seed', '42', '--threads', '+1')
if ($r.Code -ne 2) { Fail "--threads +1 应以用法错误退出码 2 失败（实际 $($r.Code)）" }

Remove-Item -LiteralPath $tempDir -Recurse -Force
Write-Host 'CLI TESTS PASSED'
exit 0
