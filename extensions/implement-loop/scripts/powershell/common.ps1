#!/usr/bin/env pwsh
# speckit-implement-loop 扩展公共函数

# 向上查找包含 .specify/ 的目录，即仓库根（spec-kit 项目标记）
function Find-RepoRoot {
    param([string]$StartDir = (Get-Location).Path)
    $resolved = Resolve-Path -LiteralPath $StartDir -ErrorAction SilentlyContinue
    $current = if ($resolved) { $resolved.Path } else { $null }
    while ($current) {
        if (Test-Path -LiteralPath (Join-Path $current ".specify") -PathType Container) {
            return $current
        }
        $parent = Split-Path $current -Parent
        if (-not $parent -or $parent -eq $current) { return $null }
        $current = $parent
    }
    return $null
}

# 从 git remote 推导 owner/repo（如 owner/repo）
function Get-GitRemoteRepo {
    param([string]$RepoRoot)
    $remoteUrl = git -C $RepoRoot remote get-url origin 2>$null
    if ($remoteUrl -match 'github\.com[/:]([^/]+)/([^/]+?)(\.git)?$') {
        return "$($Matches[1])/$($Matches[2])"
    }
    return ""
}

# 检查工作区是否干净（只看已跟踪文件；未跟踪文件仅警告）
function Test-WorktreeClean {
    param([string]$RepoRoot)
    $porcelain = git -C $RepoRoot status --porcelain --untracked-files=no 2>$null
    if ($LASTEXITCODE -ne 0) {
        [Console]::Error.WriteLine("ERROR: git status 失败：$RepoRoot")
        return $false
    }
    return [string]::IsNullOrWhiteSpace(($porcelain -join "`n"))
}

# 输出审计/汇报统一的错误行（stderr，避免污染 stdout 的 JSON/结构化输出）
function Write-Err {
    param([string]$Message)
    [Console]::Error.WriteLine($Message)
}
