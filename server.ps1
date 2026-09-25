param(
    [Parameter(Position = 0)]
    [ValidateSet('update', 'run', 'clean')]
    [string]$Action,
    # Path to the instance config, e.g. config/server.ini. May live outside this repo.
    # Only 'run' requires it; 'update' falls back to $DefaultSteamCfg.
    [Parameter(Position = 1)]
    [string]$ServerIni
)

$Root = $PSScriptRoot

# Directory for user storage
$STORAGE_DIR = Join-Path $Root "storage"

$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT

# Path to SteamCMD. Windows binary is steamcmd.exe, Linux one is steamcmd.
$SteamCmd = if ($cmd = Get-Command steamcmd.exe -ErrorAction SilentlyContinue) {
    $cmd.Source
} elseif ($cmd = Get-Command steamcmd -ErrorAction SilentlyContinue) {
    $cmd.Source
} elseif ($isWindowsHost) {
    "C:\steamcmd\steamcmd.exe"
} else {
    "/usr/games/steamcmd"
}

# Steam app ids used by 'update'. An instance config can override them, which is only
# needed for forks or tests; DayZ itself is always 223350 / 221100.
$DefaultSteamCfg = @{
    server_app_id   = '223350'
    workshop_app_id = '221100'
}

function Show-Usage {
    Write-Host "Usage: .\server run <server.ini>"
    Write-Host "       .\server update [<server.ini>]"
    Write-Host "       .\server clean"
    Write-Host ""
    Write-Host "  <server.ini> is an instance config, e.g. config/server.ini."
    Write-Host "  Copy config\server.ini.example to get one. See README.md."
    Write-Host "  'update' works without one and uses the default DayZ app ids."
    exit 1
}

# Parse an INI file: [section] headers, # comments, key = value lines.
# $Sections maps a section name to a hashtable that receives its keys.
function Read-Ini {
    param(
        [string]$Path,
        [hashtable]$Sections
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Host "ERROR: config not found: $Path"
        exit 1
    }

    $result = @{}
    foreach ($name in $Sections.Keys) { $result[$name] = @{} }
    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        $line = $line.Trim()
        $hashIndex = $line.IndexOf('#')
        if ($hashIndex -ge 0) { $line = $line.Substring(0, $hashIndex).Trim() }
        if ($line -eq '') { continue }
        if ($line -match '^\[(.+)\]$') {
            $section = $Matches[1].Trim()
            continue
        }
        if ($line -notmatch '=') { continue }
        $parts = $line -split '=', 2
        $key = $parts[0].Trim()
        $value = $parts[1].Trim()
        if ($key -eq '' -or $value -eq '') { continue }
        if ($result.ContainsKey($section)) { $result[$section][$key] = $value }
    }
    return $result
}

function Get-RequiredKey {
    param(
        [hashtable]$Section,
        [string]$Key,
        [string]$File,
        [string]$InSection
    )

    $value = $Section[$Key]
    if ([string]::IsNullOrEmpty($value)) {
        Write-Host "ERROR: [$InSection] $Key is not set in $File"
        exit 1
    }
    return $value
}

# Paths in the instance config are relative to the config file, not to this script.
function Resolve-IniPath {
    param(
        [string]$Value,
        [string]$BaseDir
    )

    if ([System.IO.Path]::IsPathRooted($Value)) { return [System.IO.Path]::GetFullPath($Value) }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $Value))
}

function Test-IniEnabled {
    param([string]$Value)
    return $Value -eq '1' -or $Value -match '(?i)^(true|yes|on)$'
}

# Render bin\battleye\beserver_x64.cfg from the [battleye] section. BattlEye reads
# its config from the folder holding BEServer_x64.dll / beserver_x64.so, which is
# what -bepath points at. That path has to be absolute: a relative one is resolved
# against a "battleye/" subfolder of the server directory by the engine. The file
# name has to match the binary in lower case or BattlEye never reads it.
function Initialize-BattlEye {
    param(
        [hashtable]$Section,
        [string]$IniPath
    )

    $beDir = Join-Path (Join-Path $Root "bin") "battleye"
    $beExe = if ($isWindowsHost) { 'BEServer_x64.dll' } else { 'beserver_x64.so' }
    if (-not (Test-Path -LiteralPath (Join-Path $beDir $beExe))) {
        Write-Host "WARNING: BattlEye is enabled in $IniPath but $(Join-Path $beDir $beExe) is missing. Run 'update' to download the server files."
    }

    $name = if ($Section.ContainsKey('config')) { $Section['config'] } else { 'beserver_x64.cfg' }
    if ([System.IO.Path]::GetFileName($name) -ne $name) {
        Write-Host "ERROR: [battleye] config must be a file name, not a path: $name"
        exit 1
    }

    $settings = @("RConPassword $(Get-RequiredKey $Section 'rcon_password' $IniPath 'battleye')")
    foreach ($key in @(@('rcon_port', 'RConPort'), @('rcon_ip', 'RConIP'), @('restrict_rcon', 'RestrictRCon'))) {
        $value = $Section[$key[0]]
        if (-not [string]::IsNullOrEmpty($value)) { $settings += "$($key[1]) $value" }
    }
    $beConfig = Join-Path $beDir $name

    # BattlEye renames its config to <name>_active_<id>.cfg when it starts, so drop
    # the leftovers rather than let them pile up and pin a stale configuration.
    $stale = [System.IO.Path]::GetFileNameWithoutExtension($name)
    Get-ChildItem -Path $beDir -Filter "${stale}_active_*.cfg" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    # Rewritten every run so a changed password or port always takes effect.
    New-Item -ItemType Directory -Path $beDir -Force | Out-Null
    [System.IO.File]::WriteAllText($beConfig, ($settings -join "`r`n") + "`r`n")
    Write-Host "BattlEye config: $beConfig"
    return $beDir
}

if (-not $Action) {
    Show-Usage
}

# The instance config, which may sit outside this repository. Only 'run' needs one.
$INI = @{ steam = @{}; run = @{}; battleye = @{}; mods = @{}; server_mods = @{} }
if ($ServerIni) {
    if (-not (Test-Path -LiteralPath $ServerIni -PathType Leaf)) {
        Write-Host "ERROR: config not found: $ServerIni"
        exit 1
    }
    $iniPath = (Resolve-Path -LiteralPath $ServerIni).Path
    $iniDir = Split-Path -Path $iniPath -Parent
    $INI = Read-Ini -Path $iniPath -Sections @{ steam = @{}; run = @{}; battleye = @{}; mods = @{}; server_mods = @{} }
    Write-Host "Instance config: $iniPath"
    if ($INI['mods'].Count -or $INI['server_mods'].Count) {
        Write-Host "NOTE: [mods]/[server_mods] in $iniPath are ignored; the mod list lives in config\mods.ini"
    }
    $CFG = $INI['steam'] + $INI['run']
} elseif ($Action -eq 'run') {
    Write-Host "ERROR: 'run' needs a <server.ini>, e.g. .\server run config\server.ini"
    Show-Usage
}

$STEAM_CFG = $DefaultSteamCfg.Clone()
foreach ($key in $DefaultSteamCfg.Keys) {
    if ($INI['steam'][$key]) { $STEAM_CFG[$key] = $INI['steam'][$key] }
}
$BATTLEYE = $INI['battleye']
$battleyeEnabled = $BATTLEYE -and (Test-IniEnabled $BATTLEYE['enabled'])

# The mod list is source controlled, so it always comes from the repository.
if ($Action -ne 'clean') {
    $modsIni = Join-Path $Root "config\mods.ini"
    $MODS_INI = Read-Ini -Path $modsIni -Sections @{ mods = @{}; server_mods = @{} }
    $MOD_MAP = $MODS_INI['mods']
    $SERVER_MOD_MAP = $MODS_INI['server_mods']
}

switch ($Action) {
    'update' {
        $SRV_DIR = Join-Path $Root "bin"
        $MOD_DIR = Join-Path $Root "mods"
        $STEAM_USER = $env:STEAM_USER
        if ([string]::IsNullOrEmpty($STEAM_USER)) {
            Write-Host "ERROR: STEAM_USER environment variable is not set."
            exit 1
        }
        if (-not $ServerIni) {
            Write-Host "Steam: app $($STEAM_CFG['server_app_id']), workshop $($STEAM_CFG['workshop_app_id']) (no instance config, using defaults)"
        }

        $params = @(
            "+force_install_dir", $SRV_DIR,
            "+login", $STEAM_USER,
            "+app_update", $STEAM_CFG['server_app_id'], "validate",
            "+quit"
        )
        & $SteamCmd @params
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: SteamCMD failed with exit code $LASTEXITCODE"
            exit 1
        }

        $modArgs = @("+force_install_dir", $MOD_DIR, "+login", $STEAM_USER)
        foreach ($id in $MOD_MAP.Keys) {
            $modArgs += "+workshop_download_item", $STEAM_CFG['workshop_app_id'], $id
        }
        foreach ($id in $SERVER_MOD_MAP.Keys) {
            $modArgs += "+workshop_download_item", $STEAM_CFG['workshop_app_id'], $id
        }
        $modArgs += "+quit"
        & $SteamCmd @modArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: SteamCMD failed with exit code $LASTEXITCODE"
            exit 1
        }

        $customDir = Join-Path $Root "custom"
        if (Test-Path $customDir) {
            Copy-Item -Path (Join-Path $customDir "*") -Destination $SRV_DIR -Recurse -Force
        }

        $linkType = if ($isWindowsHost) { 'Junction' } else { 'SymbolicLink' }

        # bin/config is a link pointing back to the source-controlled config dir.
        $rootConfig = Join-Path $Root "config"
        $binConfig = Join-Path $SRV_DIR "config"
        Remove-Item -Recurse -Force $binConfig -ErrorAction SilentlyContinue
        New-Item -ItemType $linkType -Path $binConfig -Target $rootConfig -Force | Out-Null

        $contentDir = Join-Path $MOD_DIR "steamapps\workshop\content\$($STEAM_CFG['workshop_app_id'])"
        foreach ($id in $MOD_MAP.Keys) {
            $dst = Join-Path $SRV_DIR $MOD_MAP[$id]
            $src = Join-Path $contentDir $id
            New-Item -ItemType $linkType -Path $dst -Target $src -Force | Out-Null
        }
        foreach ($id in $SERVER_MOD_MAP.Keys) {
            $dst = Join-Path $SRV_DIR $SERVER_MOD_MAP[$id]
            $src = Join-Path $contentDir $id
            New-Item -ItemType $linkType -Path $dst -Target $src -Force | Out-Null
        }

        # Root/mpmissions is the real, source-controlled mission folder.
        # bin/mpmissions is a link pointing back to it so the server finds it.
        $rootMissions = Join-Path $Root "mpmissions"
        $binMissions = Join-Path $SRV_DIR "mpmissions"
        Remove-Item -Recurse -Force $binMissions -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Path $rootMissions -Force | Out-Null
        New-Item -ItemType $linkType -Path $binMissions -Target $rootMissions -Force | Out-Null

        $keysDir = Join-Path $SRV_DIR "keys"
        New-Item -ItemType Directory -Path $keysDir -Force | Out-Null
        Copy-Item -Path (Join-Path $contentDir "*\keys\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue
        Copy-Item -Path (Join-Path $contentDir "*\Keys\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue
        Copy-Item -Path (Join-Path $contentDir "*\key\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue

        if ($battleyeEnabled) { Initialize-BattlEye -Section $BATTLEYE -IniPath $iniPath | Out-Null }
    }

    'run' {
        $SRV_DIR = Join-Path $Root "bin"
        $MODS = ''
        foreach ($value in $MOD_MAP.Values) { $MODS += "$value;" }
        $SERVER_MODS = ''
        foreach ($value in $SERVER_MOD_MAP.Values) { $SERVER_MODS += "$value;" }

        $serverConfig = Resolve-IniPath (Get-RequiredKey $CFG 'config' $iniPath 'run') $iniDir
        if (-not (Test-Path -LiteralPath $serverConfig -PathType Leaf)) {
            Write-Host "ERROR: [run] config in $iniPath points at a file that does not exist: $serverConfig"
            exit 1
        }

        $params = @(
            "-port=$($CFG['port'])",
            "-limitFPS=$($CFG['limitFPS'])",
            "-cpuCount=$($CFG['cpuCount'])",
            "-exThreads=$($CFG['exThreads'])",
            "-maxMem=$($CFG['maxMem'])",
            "-profiles=$(Join-Path $Root 'profiles')",
            "-config=$serverConfig",
            "-storage=$STORAGE_DIR",
            "-mod=$MODS",
            "-serverMod=$SERVER_MODS"
        )

        if ($battleyeEnabled) {
            $beDir = Initialize-BattlEye -Section $BATTLEYE -IniPath $iniPath
            # Lower case on purpose: the engine looks the option up in a case sensitive
            # table as "bepath". Anything else is ignored and the engine falls back to
            # its built in battleye/ folder, which is not necessarily where the binaries are.
            $params += "-bepath=$beDir"
        }

        $exe = Join-Path $SRV_DIR $(if ($isWindowsHost) { 'DayZServer_x64.exe' } else { 'DayZServer' })
        Push-Location $SRV_DIR
        try {
            & $exe @params
            exit $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }

    'clean' {
        Remove-Item -Recurse -Force (Join-Path $Root "bin"), (Join-Path $Root "mods") -ErrorAction SilentlyContinue
    }
}
