param(
    [Parameter(Mandatory = $true)][string]$NextBranch
)

$ErrorActionPreference = "Stop"

git fetch origin main
if ($LASTEXITCODE -ne 0) { throw "git fetch origin main 失败" }

git checkout $NextBranch
if ($LASTEXITCODE -ne 0) { throw "git checkout $NextBranch 失败" }

git rebase origin/main
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: rebase 到 origin/main 产生冲突，请人工解决后继续（解决后不要强制推送）。"
    exit 1
}

git push --force-with-lease origin $NextBranch
if ($LASTEXITCODE -ne 0) { throw "git push 失败" }

Write-Host "已 rebase 到最新 main 并推送 $NextBranch，CI 将重新触发。"
exit 0
