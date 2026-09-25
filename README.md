# DayZ Black Sector: Chernarus

PowerShell launcher for a DayZ dedicated server. Server files are downloaded by
the launcher; the mod list, missions and shared config are tracked in git.

Requires PowerShell 7+ (`pwsh`) or Windows PowerShell 5.1, and `steamcmd` on
PATH.

```
git clone https://github.com/ghisforlamers/BlackSectorChernarus.git
git pull
```

## Configuration

Copy the two templates and edit the copies. Neither copy is tracked in git, and
both may live anywhere, including outside this repository.

`config/server.ini.example` — launcher settings: ports, performance, BattlEye.
`config/server.conf.example` — DayZ server config: hostname, passwords, mission.

Paths inside `server.ini` resolve relative to `server.ini` itself, not to the
launcher. INI format: `[section]` headers, `key = value` lines, `#` comments.

| Section | Keys |
| --- | --- |
| `[steam]` | `server_app_id`, `workshop_app_id` (defaults 223350 / 221100) |
| `[run]` | `port`, `limitFPS`, `cpuCount`, `exThreads`, `maxMem`, `config`, `storage` |
| `[battleye]` | `enabled`, `config`, `rcon_password`, `rcon_port`, `rcon_ip`, `restrict_rcon` |

`[run] config` points at `server.conf`. `[run] storage` is the player data
folder passed to the server as `-storage`; it is optional and defaults to the
`storage` folder beside the launcher. The engine appends `storage_<instanceId>`
to it, using `instanceId` from `server.conf`. Point it at a separate volume to
run multiple instances.

## server.ps1

`update` needs only `$env:STEAM_USER`; it works without a `server.ini` and uses
the default app ids and the checked-in `config/mods.ini`. It downloads the
server, downloads the workshop mods, copies `custom/` into `bin\`, and creates
junctions (Windows) or symlinks (Linux) for the mods, `bin\config` and
`bin\mpmissions`.

`run` requires a `server.ini`. `clean` removes `bin\` and `mods\`.

```
$env:STEAM_USER = "username"
.\server.ps1 update
.\server.ps1 run    config\server.ini.example
.\server.ps1 clean
```

Passing a `server.ini` to `update` only overrides the app ids and writes the
BattlEye config early; both happen on `run` anyway.

## Mods

The mod list is shared across instances, so it lives in the checked-in
`config/mods.ini`. Edit it, then run `update`.

```
[mods]         # WorkshopID = @ModName
[server_mods]  # WorkshopID = @ServerModName
```

Mods that are not on the workshop go in `custom/` and are listed in `mods.ini`
with fake Workshop IDs (1001, 1002, ...). `update` copies them into `bin\`.

## BattlEye

With `[battleye] enabled = 1` the launcher writes `bin\battleye\beserver_x64.cfg`
next to the BattlEye binaries and starts the server with `-bepath` pointing at
that folder. The file is rewritten on every `update` and `run`, so password and
port changes always take effect. The stale `beserver_x64_active_*.cfg` copies
BattlEye leaves behind are deleted on each run.

`rcon_ip = 127.0.0.1` only accepts clients on the server itself; use `0.0.0.0`
to connect remotely, and open `rcon_port` (UDP) in the firewall. Log in with the
`passwordAdmin` from `server.conf`. `bin\ban.txt` (Steam IDs) and
`bin\battleye\bans.txt` (BattlEye GUIDs) are read while the server runs.

Both `-bepath` and the config file name must be lower case. The engine matches
the option against a case-sensitive table, and a wrong spelling is not an
error — it silently falls back to the built-in `battleye/` folder, so the server
starts without BattlEye. BattlEye likewise only reads `beserver_x64.cfg` next to
`beserver_x64.so`.
