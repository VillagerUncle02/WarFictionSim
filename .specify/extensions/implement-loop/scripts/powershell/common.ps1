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

# 从 tasks.md 解析任务 ID（T###，支持 T1000+）；-CompletedOnly 时只取 [X]
function Get-TaskIdsFromTasksFile {
    param(
        [string]$TasksFile,
        [switch]$CompletedOnly
    )
    if (-not (Test-Path -LiteralPath $TasksFile -PathType Leaf)) {
        Write-Err "ERROR: tasks.md 不存在：$TasksFile"
        return @()
    }
    $ids = @()
    foreach ($line in Get-Content -LiteralPath $TasksFile) {
        if ($CompletedOnly) {
            if ($line -match '^\s*- \[x\]\s+(T\d{3,})\b') { $ids += $Matches[1] }
        } else {
            if ($line -match '^\s*- \[[ xX]\]\s+(T\d{3,})\b') { $ids += $Matches[1] }
        }
    }
    return @($ids | Select-Object -Unique)
}

# 拉取仓库 issue（state=all，不含 PR）并按标题建立 任务ID→issue号 映射。
# 标题需包含 T### 且（若提供 TitlePattern）匹配该模式（如 ^T\d{3,}）。
# gh api 失败时返回 $null（调用方据此报错），否则返回哈希表（可为空）。
function Get-TaskIssueMap {
    param(
        [string]$Repo,
        [string]$TitlePattern = "^T\d{3,}"
    )
    # gh issue list 输出单一 JSON 数组、天然排除 PR；--paginate 的 api 方式
    # 会把多页数组拼接导致 ConvertFrom-Json 失败，不再使用。
    $ghOut = (& gh issue list --repo $Repo --state all --limit 1000 --json number,title 2>$null) | Out-String
    if ($LASTEXITCODE -ne 0) { return $null }
    $issues = @()
    try { $issues = @($ghOut | ConvertFrom-Json) } catch { }
    $map = @{}
    foreach ($issue in $issues) {
        $title = [string]$issue.title
        if ($title -match '(T\d{3,})\b') {
            $id = $Matches[1]
            if ((-not $TitlePattern -or $title -match $TitlePattern) -and -not $map.ContainsKey($id)) {
                $map[$id] = [int]$issue.number
            }
        }
    }
    return $map
}

# 检出/合并失败时，识别"untracked 文件会被覆盖"错误并给出处理建议
function Write-UntrackedConflictHint {
    param([string]$GitErrorText)
    if ($GitErrorText -match 'untracked working tree files would be overwritten') {
        Write-Err "检测到未跟踪文件与目标分支文件冲突（常见成因：文件被运行环境占用，无法 unlink，残留为 untracked）。"
        Write-Err "处理建议："
        Write-Err "  1) 关闭占用这些文件的程序（编辑器/进程）后重试；"
        Write-Err "  2) 若确认工作区内容与目标分支一致，可先 git add <冲突文件> 再重试；"
        Write-Err "  3) 内容不一致时请人工处理后再继续，勿强行覆盖。"
    }
}
