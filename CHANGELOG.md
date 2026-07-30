# Changelog

All notable changes to ShackLog are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[semantic versioning](https://semver.org/). Releases are cut by pushing a
`vX.Y.Z` tag; CI then builds the Windows installer + zip, Linux AppImage, and
macOS DMG and attaches them to the GitHub release.

## [Unreleased]

### Added
- **Grid Map** (Maps → Grid Map) — a Maidenhead grid tracker: equirectangular
  world with the AA–RR field lattice, every 4-character square worked painted
  at its true location (dim green worked, bright green confirmed — LoTW or
  QSL card, the same rule the Awards panel uses), band/mode filters, hover
  details, live repaint as QSOs are logged. The continent backdrop is
  deliberately coarse hand-laid geometry in the Section Map spirit.

### Fixed
- LoTW dialog window title rendered a literal `&&`.

## [0.4.0] - 2026-07-30

### Added
- **LoTW sign & upload + confirmations** (Tools → LoTW Upload) — exports the
  QSOs that have never been uploaded (or a chosen date range) and hands them
  to ARRL TrustedQSL (`tqsl`) for signing and upload; on success they are
  marked `LOTW_QSL_SENT=Y` with the upload date. tqsl is auto-detected and
  the certificate never leaves it. A second panel fetches your LoTW
  confirmations (website login, not the certificate passphrase) and applies
  them to the log as `LOTW_QSL_RCVD=Y`, which the Awards panel counts as
  confirmed. Brand-new LoTW users get a guided setup banner instead of an
  error when tqsl isn't installed yet.
  Schema v3 adds `LOTW_QSLSDATE`/`LOTW_QSLRDATE` (both round-trip through
  ADIF export/import). Older ShackLog builds cannot open a migrated log —
  update every machine that shares one.

### Fixed
- The startup operator chooser could open hidden behind other windows,
  making launches look broken (and once left an old installed copy driving
  the session unnoticed). It now stays on top and takes focus.
- **APRS Activity window** (Tools → APRS Activity) — connects to AetherSDR's
  KISS-over-TCP TNC (default `127.0.0.1:8001`), decodes the off-air AX.25/APRS
  traffic, and shows a live roster of heard stations with great-circle
  distance/bearing from `MY_GRIDSQUARE`, time-since-heard, APRS symbol, and a ✓
  against any call already in the log. Stale stations age out (default 1 h).
  A message row sends APRS text back out through AetherSDR. The decoder
  (`AprsDecode`) is a clean-room KISS/AX.25/APRS implementation with 23 unit
  tests (build with `-DSHACKLOG_TESTS=ON`); the socket client
  (`AprsKissClient`) auto-reconnects like the TCI client.
- **Server: live ARRL Field Day score** — `GET /api/score` returns a JSON
  breakdown (QSO points × power multiplier + bonus), classifying phone/CW/
  digital per FD rules; power multiplier and bonus are query params.
- **Server: N3FJP network mirror** — `N3fjpClient` connects to a remote N3FJP
  server and logs its QSOs into the local database.

### Changed
- **Unified ADIF parsing.** The server's WSJT-X UDP receiver used its own copy
  of the ADIF field parser; it now uses the shared `AdifReader` (same parser as
  File → Import ADIF). As a side benefit, WSJT-X ingest now gets band-from-freq
  derivation, the MODE:USB/LSB → SSB+SUBMODE fold, and QSL/LoTW/eQSL field
  handling it previously lacked. Verified end-to-end with a live FT8 datagram.

## [0.3.2] — 2026-06-12

### Added
- **Section map** (Maps menu) — a native, N3FJP-style ARRL/RAC section map.

## [0.3.1] — 2026-06-12

### Fixed
- Callsign-lookup XML parsing: QRZ/HamQTH responses were consumed whole by
  `readElementText()` on a container element, so the session id never parsed
  and login always "failed". Replaced with a leaf-text token walk.
- DX cluster duplicate-login ping-pong: two clients on one call-SSID kicked
  each other in a ~1 s reconnect storm. Reconnect backoff now only resets
  after sustained uptime, kicks are detected and surfaced to the operator,
  and a kicked client backs off to 30 s.

## [0.3.0] — 2026-06-12

### Added
- **Callsign lookup chain** — three-tier autofill of empty QSO fields at save:
  worked-before → offline cty.dat prefix resolver → online (QRZ / HamQTH /
  callook.info). Bundled `cty.dat` as a Qt resource; new Settings → Lookup tab.

## [0.2.0] — 2026-06-10

### Added
- **ADIF file import** (File → Import ADIF) — whole-file, single-transaction,
  deduplicated import via a shared byte-oriented ADIF reader. Proven on a real
  16k+ record HRD export.
- **Multi-operator logs** — one SQLite database per callsign, with a startup
  operator chooser and live *Switch Operator/Log*.
- **Awards panel** (Tools → Awards) — DXCC / WAS / WAC / WAZ / grids, worked vs
  confirmed, with chase lists.
- **"How far?"** button — opens a PSK Reporter map filtered to your callsign
  and current band.
- **Desktop WSJT-X ingest** — logs WSJT-X "Secondary UDP" QSOs straight into
  the open log (solo-operating mode).
- **Server (Field Day infrastructure)** — headless companion exposing an HTTP
  API (`/api/qsos`) and an N3FJP-compatible TCP endpoint, writing into the
  shared logbook with schema versioning, an audit trail, and soft-delete;
  ingests WSJT-X Secondary-UDP spots.

### Changed
- Cross-platform verified on Windows, Linux, and macOS. Build fixed on Qt 6.4
  (`QWebSocket::errorOccurred` is Qt 6.5+).

## [0.1.2] — 2026-05-08

### Added
- Initial release: SQLite-backed logbook, TCI WebSocket auto-fill of freq /
  band / mode, quick QSO entry with live duplicate check, filterable QSO
  table, full-fidelity QSO editor, ADIF 3.1.4 and Cabrillo 3.0 export, and a
  cross-platform GitHub Actions release workflow.

[Unreleased]: https://github.com/nigelfenton/shacklog/compare/v0.3.2...HEAD
[0.3.2]: https://github.com/nigelfenton/shacklog/releases/tag/v0.3.2
[0.3.1]: https://github.com/nigelfenton/shacklog/releases/tag/v0.3.1
[0.3.0]: https://github.com/nigelfenton/shacklog/releases/tag/v0.3.0
[0.2.0]: https://github.com/nigelfenton/shacklog/releases/tag/v0.2.0
[0.1.2]: https://github.com/nigelfenton/shacklog/releases/tag/v0.1.2
