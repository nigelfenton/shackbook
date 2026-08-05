# ShackLog

Standalone ham radio logbook for Windows / Linux / macOS.

ShackLog is a Qt 6 desktop application that keeps a SQLite-backed QSO log and
pulls live frequency / mode information from any radio that speaks the **TCI**
(Transceiver Control Interface) protocol over WebSocket — AetherSDR,
ExpertSDR2, SunSDR, and others. It fills in callsign details automatically,
tracks awards, watches the DX cluster, and exports to **ADIF 3.x** and
**Cabrillo 3.0**.

It also ships an optional headless **server** for multi-station operating —
mirroring an N3FJP network, ingesting WSJT-X spots, and serving a live
Field Day score over HTTP.

## Download

Pre-built binaries — including a Windows installer — are on the
[**Releases page**](https://github.com/nigelfenton/shacklog/releases/latest):

- **Windows installer** — `ShackLog-Setup-<version>-windows-x64.exe` (double-click)
- **Windows portable zip** — `ShackLog-<version>-windows-x64.zip` (unzip, run `ShackLog.exe`)
- **Linux AppImage** — `ShackLog-<version>-linux-x86_64.AppImage` (`chmod +x` and run)
- **macOS DMG (Apple Silicon)** — `ShackLog-<version>-macos-arm64.dmg`

Or build from source — see [Build](#build).

## Features

### Logging
- **Live freq / band / mode auto-fill** from a TCI server (default `ws://127.0.0.1:40001`), with auto-reconnect
- Quick QSO entry: callsign + RST sent/received + comment, then `SAVE`
- Real-time **duplicate-check** warning as you type a callsign
- Full-fidelity QSO editor (Core / Other Station / My Station / Contest / Notes & QSL)
- Filterable QSO table — text search across call/name/QTH/grid/comment, plus band / mode / contest selectors
- **Multi-operator logs** — one database per callsign, live *Switch Operator/Log*
- **Which radio made each QSO** — the radio's name is taken from the TCI
  connection, shown in the status bar while you work, and stored on every QSO
  (exported as ADIF `MY_RIG`). Because TCI reports the *application* rather
  than the rig — AetherSDR answers "AetherSDR" for any radio behind it — you
  can set a **nickname** per TCI host/port to tell two rigs apart. A QSO logged
  with no radio connected records none, rather than guessing.

### Callsign lookup
Three-tier autofill of an empty QSO's name / QTH / grid / state / country:
1. **Worked-before** — reuses details from your previous QSO with that station
2. **cty.dat** — bundled offline AD1C prefix resolver (country / continent / CQ / ITU zones)
3. **Online** — QRZ.com (XML, paid), HamQTH (free), and callook.info (US, no account)

### Awards & spotting
- **Awards panel** (Tools → Awards): DXCC / WAS / WAC / WAZ / grids, worked vs confirmed, chase lists
- **DX cluster** client with configurable login suffix and duplicate-login handling
- **POTA** spot integration
- **"How far?"** button — opens a PSK Reporter map of who's hearing you, filtered to your band
- **Section map** (Maps menu) — native ARRL/RAC section map, N3FJP-style

### Looking after the log
Your log is the one file in the shack that cannot be remade, so ShackLog
treats it that way — all of this happens on its own, with nothing to configure:
- **Verified backups** — a compacted snapshot taken at most weekly, and always
  before a database upgrade. Each one is reopened and checked before it counts
  as a backup. They live in a `backups/` folder beside the log.
- **Checked on open** — a fast integrity check every launch, the thorough one
  only if that finds something.
- **Quarantine and restore** — a log that fails its check has the damaged files
  moved aside (never deleted) and the newest verified backup put in their
  place, with a dialog explaining what happened. If there is no backup to
  restore, ShackLog keeps logging on the damaged file rather than locking you
  out mid-contest — but tells you clearly.

### Import / export
- **ADIF import** (File → Import ADIF) — whole-file, deduplicated, fast (proven on 16k+ record logs)
- **ADIF 3.1.4 export** with the full standard field set
- **Cabrillo 3.0 export** with operator-defined header categories

### Server (optional, for multi-station / Field Day)
A headless companion (`shacklog-server`) that:
- Mirrors a remote **N3FJP** network so its QSOs land in your log
- Ingests **WSJT-X** "Secondary UDP" spots (udp/1100) straight into the log
- Serves a **live ARRL Field Day score** breakdown over HTTP (`/api/score`)
- Exposes a REST API (`/api/qsos`) with schema versioning, audit trail, and soft-delete

All operator settings (MY_CALL, grid, power, TCI host/port, Cabrillo headers,
lookup credentials, cluster login) live inside the database — no external
config files.

## Build

Requirements:
- Qt 6.2 or newer with the `Sql` and `WebSockets` modules
- CMake 3.20+
- A C++17 compiler (MSVC, GCC, or Clang)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binaries are `build/ShackLog` (the logbook) and
`build/shacklog-server` (the optional server), plus `.exe` on Windows.
On Windows, `build.bat` also handles the MSVC environment setup.

The unit tests are opt-in, and cover the durability layer, the APRS decoder,
and per-QSO radio attribution:

```sh
cmake -B build-tests -G Ninja -DSHACKLOG_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

On Windows, `build-tests.bat` does the same. A build is marked `-dev` in the
window title unless configured with `-DSHACKLOG_RELEASE=ON`, so it is always
obvious whether you are running an installed release or something you built.

## Database location

The QSO database is auto-created on first run (one file per operator callsign):

| OS      | Path                                                                  |
|---------|-----------------------------------------------------------------------|
| Windows | `%LOCALAPPDATA%\G0JKN\ShackLog\shacklog-<CALL>.sqlite`                 |
| Linux   | `~/.local/share/G0JKN/ShackLog/shacklog-<CALL>.sqlite`                 |
| macOS   | `~/Library/Application Support/G0JKN/ShackLog/shacklog-<CALL>.sqlite`  |

`<CALL>` is the operator you chose at startup. The `G0JKN` folder is the
application's publisher, not your callsign — it is the same for everyone.

That one file is the entire log — copy it and you have copied everything.
ShackLog keeps its own verified backups in a `backups/` folder beside it (see
[Looking after the log](#looking-after-the-log)), but those live on the same
disk, so keep a copy somewhere else as well.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — what changed in each release
- [ROADMAP.md](ROADMAP.md) — what's planned next

## License

MIT. See [LICENSE](LICENSE).

73 de G0JKN / W3.
