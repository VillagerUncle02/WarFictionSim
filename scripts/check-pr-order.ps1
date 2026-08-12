param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [Parameter(Mandatory = $true)][int]$PRNumber,
    [switch]$IgnoreOrder
)

$ErrorActionPreference = "Continue"

# 忽略模式：靠后创建的 PR 不等待前序 PR 合并（链式多 PR 并行时避免 CI 阻断）。
if ($IgnoreOrder) {
    Write-Host "已按配置忽略前序 PR 已合并检查（-IgnoreOrder）。"
    exit 0
}

# 链式队列顺序检查：是否存在比当前 PR 编号更小且仍 open 的 PR（前序未合并）。
# 有 → exit 1（本分支 CI 标红并提示先合并前序）；无 → exit 0。
$openJson = gh pr list --repo $Repo --state open --base main --json number 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: gh pr list 失败（网络/权限）。"
    exit 2
}
try { $openPRs = $openJson | ConvertFrom-Json } catch { $openPRs = @() }
$before = @($openPRs | ForEach-Object { [int]$_.number } | Where-Object { $_ -lt $PRNumber } | Sort-Object)
if ($before.Count -gt 0) {
    Write-Host "请先合并前序 PR：#$($before -join ', #')（链式队列要求按序合并）。"
    exit 1
}
Write-Host "无前序未合并 PR，通过。"
exit 0
