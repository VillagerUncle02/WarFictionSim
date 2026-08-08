param(
    [Parameter(Mandatory = $true)][string]$Title,
    [string]$Issue = "",
    [string]$Base = "main"
)

$ErrorActionPreference = "Stop"

if (-not $Issue) {
    Write-Host "未提供 -Issue，PR 将不关联 issue。"
}

$issueNums = @()
if ($Issue) {
    # 支持逗号分隔的多个 issue：-Issue "T001,T002,T003"
    $issueNums = $Issue -split ',' | ForEach-Object { ($_ -replace '^T', '').Trim() } | Where-Object { $_ -ne "" }
}
$closesLines = if ($issueNums.Count -gt 0) { ($issueNums | ForEach-Object { "Closes #$_" }) -join "`n" } else { "" }
$body = @"
## 变更说明

- 任务：$Issue
$closesLines
- 生成方式：自动实现循环（speckit-implement-loop），待人工审查。
"@

gh pr create --base $Base --title $Title --body $body
if ($LASTEXITCODE -ne 0) { throw "gh pr create 失败" }

Write-Host "PR 已创建，等待人工审查与合并。"
