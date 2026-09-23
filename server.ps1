param(
    [ValidateSet('update', 'run', 'clean')]
    [string]$Action
)

$Root = $PSScriptRoot

# Directory for user storage
$STORAGE_DIR = Join-Path $Root "storage"

# Path to SteamCMD. Falls back to the default Windows install location.
$SteamCmd = if (Get-Command steamcmd.exe -ErrorAction SilentlyContinue) {
    (Get-Command steamcmd.exe).Source
} else {
    "C:\steamcmd\steamcmd.exe"
}

function Show-Usage {
    Write-Host "Usage: .\server.ps1 {update|run|clean}"
    exit 1
}

$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT

# Parse the shared config/server.ini
function Read-ServerIni {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        Write-Host "ERROR: config not found: $Path"
        exit 1
    }

    $result = @{
        CFG = @{}
        MOD_MAP = @{}
        SERVER_MOD_MAP = @{}
    }
    $section = ''
    foreach ($line in Get-Content $Path) {
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
        if ($key -eq '') { continue }
        switch ($section) {
            'steam' { $result.CFG[$key] = $value }
            'run' { $result.CFG[$key] = $value }
            'mods' { $result.MOD_MAP[$key] = $value }
            'server_mods' { $result.SERVER_MOD_MAP[$key] = $value }
        }
    }
    return $result
}

$cfgPath = Join-Path $PSScriptRoot "config\server.ini"
$ini = Read-ServerIni -Path $cfgPath
$CFG = $ini.CFG
$MOD_MAP = $ini.MOD_MAP
$SERVER_MOD_MAP = $ini.SERVER_MOD_MAP

if (-not $Action) {
    Show-Usage
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

        $params = @(
            "+force_install_dir", $SRV_DIR,
            "+login", $STEAM_USER,
            "+app_update", $CFG['server_app_id'], "validate",
            "+quit"
        )
        & $SteamCmd @params
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: SteamCMD failed with exit code $LASTEXITCODE"
            exit 1
        }

        $modArgs = @("+force_install_dir", $MOD_DIR, "+login", $STEAM_USER)
        foreach ($id in $MOD_MAP.Keys) {
            $modArgs += "+workshop_download_item", $CFG['workshop_app_id'], $id
        }
        foreach ($id in $SERVER_MOD_MAP.Keys) {
            $modArgs += "+workshop_download_item", $CFG['workshop_app_id'], $id
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

        $contentDir = Join-Path $MOD_DIR "steamapps\workshop\content\$($CFG['workshop_app_id'])"
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
    }

    'run' {
        $SRV_DIR = Join-Path $Root "bin"
        $MODS = ''
        foreach ($value in $MOD_MAP.Values) { $MODS += "$value;" }
        $SERVER_MODS = ''
        foreach ($value in $SERVER_MOD_MAP.Values) { $SERVER_MODS += "$value;" }

        $params = @(
            "-port=$($CFG['port'])",
            "-limitFPS=$($CFG['limitFPS'])",
            "-cpuCount=$($CFG['cpuCount'])",
            "-exThreads=$($CFG['exThreads'])",
            "-maxMem=$($CFG['maxMem'])",
            "-profiles=$(Join-Path $Root 'profiles')",
            "-config=$(Join-Path $Root $CFG['config'])",
            "-storage=$STORAGE_DIR",
            "-mod=$MODS",
            "-serverMod=$SERVER_MODS"
        )
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
