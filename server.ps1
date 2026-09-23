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
        if ($line -eq '' -or $line.StartsWith('#')) { continue }
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

        $contentDir = Join-Path $MOD_DIR "steamapps\workshop\content\$($CFG['workshop_app_id'])"
        $linkType = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
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

        $keysDir = Join-Path $SRV_DIR "keys"
        New-Item -ItemType Directory -Path $keysDir -Force | Out-Null
        Copy-Item -Path (Join-Path $contentDir "*\keys\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue
        Copy-Item -Path (Join-Path $contentDir "*\Keys\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue
        Copy-Item -Path (Join-Path $contentDir "*\key\*.bikey") -Destination $keysDir -Force -ErrorAction SilentlyContinue
    }

    'run' {
        $SRV_DIR = Join-Path $Root "bin"
        $MODS = ($MOD_MAP.Values -join ';')
        $SERVER_MODS = ($SERVER_MOD_MAP.Values -join ';')

        $params = @(
            "-port=$($CFG['port'])",
            "-limitFPS=$($CFG['limitFPS'])",
            "-cpuCount=$($CFG['cpuCount'])",
            "-exThreads=$($CFG['exThreads'])",
            "-maxMem=$($CFG['maxMem'])",
            "-profiles=$(Join-Path $Root 'profiles')",
            "-config=$(Join-Path $Root $CFG['config'])",
            "-mission=$($CFG['mission'])",
            "-storage=$STORAGE_DIR",
            "-mod=$MODS",
            "-serverMod=$SERVER_MODS"
        )
        $exe = Join-Path $SRV_DIR "DayZServer_x64.exe"
        & $exe @params
        exit $LASTEXITCODE
    }

    'clean' {
        Remove-Item -Recurse -Force (Join-Path $Root "bin"), (Join-Path $Root "mods") -ErrorAction SilentlyContinue
    }
}
