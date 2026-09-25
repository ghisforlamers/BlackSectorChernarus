# DayZ Black Sector: Chernarus

DayZ server launcher scripts for PowerShell.

Install:
```
git clone https://github.com/ghisforlamers/BlackSectorChernarus.git
cd BlackSectorChernarus
```

Update:
```
git pull
```

## PowerShell (Windows / Linux)

Requirements: PowerShell 7+ (`pwsh`) or Windows PowerShell 5.1, `steamcmd`
(on PATH).

## Configuration

Only `run` needs a config. `update` works on its own, it just uses the default
DayZ app ids (223350 / 221100) and the checked in `config/mods.ini`.

A per-instance `server.ini` is **not** tracked by this repository. Copy the
templates out of `config/` and customize them:

```
cp config/server.ini.example config/server.ini       # Copy-Item on Windows
cp config/server.conf.example config/server.conf
```

Then edit the copies: `config/server.ini` sets the ports, performance and
BattlEye options, `config/server.conf` is the DayZ server config (hostname,
passwords, mission settings).

`server.ini` may live anywhere, including outside this repository. Paths inside it
are resolved relative to the `server.ini` itself, so `config = server.conf` means
"<that folder>/server.conf" no matter where the file is kept.

```
$env:STEAM_USER = "username"

.\server update                     # download server, mods, link as junctions / symlinks

.\server run    config/server.ini    # launch DayZ server (needs server.ini)

.\server clean                      # remove bin\ and mods\
```

`update` also takes an optional `server.ini`, but only to override the app ids or to
write the BattlEye config before the first run. Both happen on `run` anyway.

### `server.ini`

INI format: `[section]` headers, `#` comments, `key = value` lines.

```
[steam]        # server_app_id, workshop_app_id
[run]          # port, limitFPS, cpuCount, exThreads, maxMem, config
[battleye]     # enabled, config, rcon_password, rcon_port, rcon_ip, restrict_rcon
```

`[run] config` points at the DayZ server config (`config/server.conf` by default).

`[battleye]` is described below.

### Mods

The mod list is shared, so it lives in `config/mods.ini` and is checked in:

```
[mods]         # WorkshopID = @ModName
[server_mods]  # WorkshopID = @ServerModName
```

Add or remove mods by editing `config/mods.ini`, then run `update` to download and
link them.

Mods that aren't in the workshop are added to `custom`. They also need to be added
to `mods.ini` with fake Workshop IDs (1001, 1002, ...).

### BattlEye

With `[battleye] enabled = 1` the launcher writes `bin\battleye\beserver_x64.cfg`
next to the BattlEye binaries that ship with the server, and starts the server with
`-bepath` pointing at that folder. The file is rewritten on every `update`/`run`,
so a changed password or port always takes effect:

```
[battleye]
enabled = 1
config = beserver_x64.cfg     # file name written into the BattlEye folder
rcon_password = change-me     # keep this private
rcon_port = 27015             # open this UDP port in the firewall
rcon_ip = 127.0.0.1
restrict_rcon = 0
```

Connect an RCON client (Dart, BEC, ...) to `rcon_ip`:`rcon_port` with
`rcon_password`, then use `passwordAdmin` from `server.conf` to log in with
`#login`. `rcon_ip = 127.0.0.1` only accepts tools running on the server itself; use
`0.0.0.0` if you connect from another machine, and open the port in the firewall.
`bin\ban.txt` (Steam IDs) and `bin\battleye\bans.txt` (BattlEye GUIDs) are read
while the server runs.

Note that the option is spelled `-bepath`, in lower case, because the engine matches
it against a case sensitive table. A misspelled option is not an error, the engine
just falls back to its built in `battleye/` folder, so the server starts without
BattlEye wherever the binaries happen to be. The config file name has to stay lower
case as well: BattlEye looks for `beserver_x64.cfg` next to `beserver_x64.so` and
ignores any other spelling, which is why the launcher rewrites both the config and
the stale `beserver_x64_active_*.cfg` copy BattlEye leaves behind.
