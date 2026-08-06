# Roadmap

Where ShackLog is headed. Nothing here is a promise or a schedule — it's the
current list of things worth building next, roughly in priority order. Done
items live in the [CHANGELOG](CHANGELOG.md).

## Near term

- **Native CAT, without Hamlib** — v0.6.0 reaches non-TCI radios through Hamlib's
  `rigctld`, which works but asks the operator to install and run a separate
  program. For what a logbook actually needs — frequency, mode, and the radio's
  name — that is a large dependency: three commands out of a library built to
  handle ~200 rigs completely. Reading those directly over the serial port is
  modest per family: Kenwood is plain ASCII (`FA;`), Icom CI-V and Yaesu are short
  binary frames. Covering those three families would let most radios work with no
  install step at all.
  - ⚠ Keep the `rigctld` path as the escape hatch. It covers the long tail of
    older and stranger radios, and — the real argument — it lets ShackLog and
    WSJT-X share one radio instead of fighting over one COM port.
- **Session Map** ([#3](https://github.com/nigelfenton/shacklog/issues/3)) —
  plot where *this evening's* contacts went, not the whole log: band-coloured
  points with great-circle lines, on the existing Grid Map engine. Answers "did
  that new antenna change the footprint?". **Session definition decided: a gap
  of more than 2 hours starts a new session** — keeps an evening that crosses
  midnight UTC in one piece, which is what a plain date filter gets wrong for US
  operators. #2 has shipped, so the radio is recorded per QSO and a session map
  can distinguish rigs.
- **Visual stats dashboard** — the awards data is computed today but only shown
  as a text report. A visual panel (worked/confirmed by band + mode + DXCC,
  a simple chart) would make it far more useful at a glance.
- **DXCC entity numbers offline** — `cty.dat` already resolves country names
  and zones; adding the numeric DXCC entity id (and current/deleted status)
  would complete offline DXCC tracking without an online lookup.

## Awards, phase 2

- Per-band awards (DXCC by band, VUCC), DXCC Challenge, and current-vs-deleted
  entity validation, building on the awards summary that already exists.

## Contesting

- **Super Check Partial** — a `master.scp` callsign-hint dropdown during entry.
- **Cabrillo validation** — sanity-check a log against the selected contest's
  rules before export.
- **QSO party mode** ([#4](https://github.com/nigelfenton/shacklog/issues/4)) —
  a per-contest layout, a live worked/needed county table, and a calendar of
  what is on now or coming up. `cnty`/`state` are already stored per QSO and
  `contest_id` is already indexed, so the table is a query over existing data;
  the missing piece is a per-state county reference list
  ([#5](https://github.com/nigelfenton/shacklog/issues/5)), without which the
  table can only show *worked*, never *needed*. Shares the
  contest-definition structure the two items above also need — worth building
  that once for all four rather than three separate notions of "which contest".

## Multi-station / server

- **Live remote-log mode** — view and log against a running `shacklog-server`
  directly, rather than pulling a periodic snapshot.
- Broaden the Field Day scoring/server work into a general multi-station
  networked-logging story.

## Housekeeping / tech debt

- Ongoing: keep the client and server sharing one implementation of each
  concern (ADIF parsing is now unified; watch for future drift).

---

Ideas and pull requests welcome — open an issue on
[GitHub](https://github.com/nigelfenton/shacklog/issues).
