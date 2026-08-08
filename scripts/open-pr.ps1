param(
    [Parameter(Mandatory = $true)][string]$Title,
    [string]$Issue = "",
    [string]$Base = "main"
)

$ErrorActionPreference = "Stop"

if (-not $Issue) {
    Write-Host "未提供 -Issue，PR 将不关联 issue。"
}

$issueNum = ($Issue -replace '^T', '')
$body = @"
## 变更说明

- 任务：$Issue
- 生成方式：自动实现循环（speckit-implement-loop），待人工审查。
"@

if ($Issue) {
    $body = @"
## 变更说明

- 任务：$Issue
- Closes #$issueNum
- 生成方式：自动实现循环（speckit-implement-loop），待人工审查。
"@
}

gh pr create --base $Base --title $Title --body $body
if ($LASTEXITCODE -ne 0) { throw "gh pr create 失败" }

Write-Host "PR 已创建，等待人工审查与合并。"
