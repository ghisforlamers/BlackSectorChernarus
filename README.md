# DayZFallenEmpires

DayZ server launcher scripts for Linux (bash) and Windows (PowerShell).

Install:
```
git clone https://github.com/ghisforlamers/DayZFallenEmpires.git
cd DayZFallenEmpires
```

Both launchers use the same commands: `update`, `run`, `clean`.

Update:
```
git pull
```

## Linux (bash)

Requirements: `bash`, `steamcmd`, `ln`, `cp`. The Steam download requires a
login; set it via the `STEAM_USER` environment variable.

```
STEAM_USER="username" ./server update   # download server, mods, link keys
./server run                           # launch DayZServer
./server clean                         # remove bin/ and mods/
```

## Windows (PowerShell)

Requirements: PowerShell 7+ (`pwsh`) or Windows PowerShell 5.1, `steamcmd.exe`
(`C:\steamcmd\steamcmd.exe` by default, or on PATH), and a DayZ dedicated
server in `bin\`. Run PowerShell as the user that hosts the server, and allow
local scripts once:

```
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
$env:STEAM_USER = "username"
.\server.ps1 update                     # download server, mods, link as junctions
.\server.ps1 run                        # launch DayZServer_x64.exe
.\server.ps1 clean                      # remove bin\ and mods\
```

## Configuration

Both launchers read the same `config/server.ini` (INI format: `[section]`
headers, `#` comments, `key = value` lines):

```
[steam]        # server_app_id, workshop_app_id
[run]          # port, limitFPS, cpuCount, exThreads, maxMem, config, mission
[mods]         # WorkshopID = @ModName
[server_mods]  # WorkshopID = @ServerModName
```

Add or remove mods by editing the `[mods]`/`[server_mods]` sections, then run
`update` to download and link them.