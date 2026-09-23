# DayZFallenEmpires

DayZ server launcher scripts for PowerShell.

Install:
```
git clone https://github.com/ghisforlamers/DayZFallenEmpires.git
cd DayZFallenEmpires
```

Update:
```
git pull
```

## PowerShell (Windows / Linux)

Requirements: PowerShell 7+ (`pwsh`) or Windows PowerShell 5.1, `steamcmd`
(on PATH).

```
$env:STEAM_USER = "username"
.\server.ps1 update                     # download server, mods, link as junctions / symlinks

.\server.ps1 run                        # launch DayZ server

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

Custom mods are added to `custom`. They also need to be added to `server.ini` with fake Workshop IDs.

The DayZ server config lives in `config/server.cfg`.
