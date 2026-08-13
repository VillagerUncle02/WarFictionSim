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
        throw "tasks.md 不存在：$TasksFile"
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

# 获取"相对基线分支新增完成"的任务 ID：链式 PR 只应 Closes 本分支新完成的任务，
# 前序 PR 已完成的 [x]（其 issue 已关闭）不应重复出现在正文。
# TasksFile 为仓库内绝对路径；基线 tasks.md 通过 `git show <BaseRef>:<rel>` 读取。
# -BaseRef 默认 origin/main；链式并行 PR（前序未合并）应传前序分支（如 origin/feature/xxx），
# 使差集只含本分支新完成的任务。
# 基线不可读时的回退语义（预期行为，非错误）：
#   - 首次 PR（tasks.md 尚未合入 main）或基线漂移：回退为全量已完成任务并 Write-Err 警告，
#     此时正文可能包含前序 PR 已关闭的 issue（冗余但不阻断）；调用方可人工核对。
#   - 正常链式流程：基线可读，仅返回本分支新完成的任务。
function Get-NewCompletedTaskIds {
    param(
        [string]$TasksFile,
        [string]$RepoRoot,
        [string]$BaseRef = "origin/main"
    )
    $current = @(Get-TaskIdsFromTasksFile -TasksFile $TasksFile -CompletedOnly)
    $rel = $TasksFile
    if ($rel.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $rel.Substring($RepoRoot.Length).TrimStart('\', '/')
    }
    $rel = $rel -replace '\\', '/'
    $baseText = git -C $RepoRoot show "${BaseRef}:$rel" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $baseText) {
        Write-Err "WARN: 无法读取 $BaseRef 的 tasks.md（$rel），回退为收集全部已完成任务。"
        return $current
    }
    $baseSet = @{}
    foreach ($line in $baseText) {
        if ($line -match '^\s*- \[x\]\s+(T\d{3,})\b') { $baseSet[$Matches[1]] = $true }
    }
    return @($current | Where-Object { -not $baseSet.ContainsKey($_) })
}

# PR 正文 Closes 块管理：
# 脚本生成的 Closes 放在 HTML 注释标记区内（GitHub 渲染时不可见），
# 更新时只替换标记区，保留人工写的 Closes # 行（如"本 PR 还修复了非任务 issue"）。
# 旧格式正文（无标记、v1.1.0 生成）首次同步时清理所有裸 Closes 行并迁移到标记区，
# 迁移前会给出提示（旧格式无法区分人工行与脚本行，需人工审查）。
function Set-ClosesBlock {
    param(
        [string]$Body,
        [string]$ClosesLines,
        [switch]$MigrateLegacy
    )
    $start = "<!-- implement-loop:closes:start -->"
    $end = "<!-- implement-loop:closes:end -->"
    $newBody = $Body

    if ($MigrateLegacy -and $newBody -notmatch [regex]::Escape($start)) {
        # 旧格式（仅 sync-pr-closes 更新既有 PR 时启用）：一次性清理裸 Closes 行并迁移（会提示）
        if ($newBody -match '(?m)^\s*Closes\s+#\d+\s*$') {
            Write-Host "WARN: 检测到旧格式 Closes 行（无标记区），将清理所有裸 Closes 行并迁移到标记区；若其中有手工关联的 issue，请审查后人工补回。"
            $newBody = [regex]::Replace($newBody, '(?m)^\s*Closes\s+#\d+\s*$', '')
            $newBody = $newBody.TrimEnd()
        }
    } else {
        # 新格式：只替换标记区，人工行不受影响
        $pattern = '(?s)' + [regex]::Escape($start) + '.*?' + [regex]::Escape($end) + '\s*'
        $newBody = [regex]::Replace($newBody, $pattern, '')
        $newBody = $newBody.TrimEnd()
    }

    if ($ClosesLines) {
        $block = $start + "`n" + $ClosesLines + "`n" + $end
        $newBody = $newBody + "`n`n" + $block
    }
    return $newBody
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
