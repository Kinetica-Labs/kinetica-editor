# Kinetica on Windows in one command: Python checked (asked before anything is installed), the editor from its latest
# release unpacked into %LOCALAPPDATA%\Kinetica\editor, and the plugin added to Claude and Codex when their commands are there.
# Nothing is installed without a yes (or -Yes); run it again to update. Linux and macOS: install.sh.
#
#   irm https://raw.githubusercontent.com/Kinetica-Labs/kinetica-editor/main/install.ps1 | iex
#   .\install.ps1 [-Yes] [-Dir DIR] [-NoClaude]
param([switch]$Yes, [string]$Dir = "$env:LOCALAPPDATA\Kinetica\editor", [switch]$NoClaude)
$ErrorActionPreference = "Stop"
$Repo = "Kinetica-Labs/kinetica-editor"

function Say($text) { Write-Host $text -ForegroundColor Cyan }
function Ask($question) { if ($Yes) { return $true }; $a = Read-Host "$question [y/N]"; return $a -match '^[yYдД]' }

# 1. Python 3.8+: the MCP server, the console's scripts and the plugins run on it (its standard library only).
$python = $null
foreach ($name in "python", "py") {
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found -and $found.Source -notmatch 'WindowsApps') { # not the Store's empty alias
        & $name -c "import sys; sys.exit(sys.version_info < (3, 8))" 2>$null
        if ($LASTEXITCODE -eq 0) { $python = $name; break }
    }
}
if (-not $python) {
    Say "Нужен Python 3.8 или новее: на нём MCP-сервер, скрипты и плагины."
    if ((Get-Command winget -ErrorAction SilentlyContinue) -and (Ask "Поставить его через winget (Python.Python.3.12)?")) {
        winget install --id Python.Python.3.12 --exact --silent --accept-package-agreements --accept-source-agreements
        $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
        $python = "python"
    } else {
        Say "Поставьте Python с python.org (галочка «Add python.exe to PATH») и запустите этот скрипт снова."; exit 1
    }
}
Say "Python: $(& $python --version)"

# 2. The editor: the latest release, the previous one kept beside it until this one is in.
$url = "https://github.com/$Repo/releases/latest/download/kinetica-windows.zip"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("kinetica-" + [Guid]::NewGuid())
New-Item -ItemType Directory $tmp | Out-Null
try {
    Say "Скачиваю $url"
    Invoke-WebRequest -Uri $url -OutFile "$tmp\kinetica-windows.zip" -UseBasicParsing
    Expand-Archive "$tmp\kinetica-windows.zip" -DestinationPath "$tmp\new"
    New-Item -ItemType Directory (Split-Path $Dir) -Force | Out-Null
    if (Test-Path $Dir) { Remove-Item "$Dir.old" -Recurse -Force -ErrorAction SilentlyContinue; Move-Item $Dir "$Dir.old" }
    Move-Item "$tmp\new" $Dir
    Remove-Item "$Dir.old" -Recurse -Force -ErrorAction SilentlyContinue
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
$program = "$Dir\kinetica-windows\kinetica.exe"
Say "Редактор: $program"

# 3. The editor's plugins beside it - formulas, a step's card and the camera, chess, terrain, motors, the depz sensors:
# a clone with its submodules (the formulas' typesetter), pulled again on the next run; a working copy of one's own is
# left alone. No git: the archive, without the formulas' typesetter.
$Plugins = Join-Path (Split-Path $Dir) "plugins"
$PluginsRepo = "https://github.com/Kinetica-Labs/kinetica-plugins"
$git = Get-Command git -ErrorAction SilentlyContinue
if (Test-Path "$Plugins\.git") {
    if ((git -C $Plugins rev-parse --abbrev-ref HEAD 2>$null) -eq "main") {
        Say "Обновляю плагины редактора: $Plugins"
        git -C $Plugins pull -q --ff-only --recurse-submodules
    } else { Say "Плагины редактора - ваша рабочая копия, не трогаю: $Plugins" }
} elseif (Test-Path $Plugins) {
    Say "Папка $Plugins уже есть и это не копия kinetica-plugins: не трогаю"
} elseif ($git) {
    Say "Скачиваю плагины редактора в $Plugins"
    git clone -q --depth 1 --recurse-submodules --shallow-submodules $PluginsRepo $Plugins
} else {
    Say "git нет: плагины архивом, формулы без своего набора (поставьте git и запустите скрипт снова)"
    $zip = Join-Path ([IO.Path]::GetTempPath()) ("kinetica-plugins-" + [Guid]::NewGuid() + ".zip")
    Invoke-WebRequest -Uri "$PluginsRepo/archive/refs/heads/main.zip" -OutFile $zip -UseBasicParsing
    Expand-Archive $zip -DestinationPath (Split-Path $Dir)
    Move-Item (Join-Path (Split-Path $Dir) "kinetica-plugins-main") $Plugins
    Remove-Item $zip -Force
}

# 4. Claude: the plugin from this repository's marketplace - the editor's MCP server (on this Python), its skills.
if (-not $NoClaude -and (Get-Command claude -ErrorAction SilentlyContinue)) {
    Say "Подключаю плагин Kinetica к Claude"
    claude plugin marketplace add $Repo
    claude plugin install kinetica@kinetica --config "python=$python" --config "editor=$Dir" --config "plugins=$Plugins"
} elseif (-not $NoClaude) {
    Say "Команды claude нет: плагин поставьте в Claude сами - /plugin marketplace add $Repo, затем /plugin install kinetica@kinetica"
}

# 5. Codex and ChatGPT: the same plugin (plugin.json, the Agent Plugins schema); its server runs as python3.
if (-not $NoClaude -and (Get-Command codex -ErrorAction SilentlyContinue)) {
    Say "Подключаю плагин Kinetica к Codex и ChatGPT"
    codex plugin marketplace add $Repo
    codex plugin add kinetica@kinetica
    if (-not (Get-Command python3 -ErrorAction SilentlyContinue)) { Say "Codex запускает сервер Kinetica командой python3: её нет в PATH - поставьте Python с python.org или из winget" }
}

Say "Готово. Запуск: $program"
