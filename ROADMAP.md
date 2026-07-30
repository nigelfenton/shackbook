# Roadmap

Where ShackLog is headed. Nothing here is a promise or a schedule — it's the
current list of things worth building next, roughly in priority order. Done
items live in the [CHANGELOG](CHANGELOG.md).

## Near term

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
