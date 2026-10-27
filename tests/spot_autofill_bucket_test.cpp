// Spot auto-fill must not survive a QSY to a different station.
//
// The bug: tryAutofillFromSpot()'s no-hit path kept the previous auto-fill
// unconditionally, so moving to an unspotted frequency left the last
// station's call and comment in the entry fields — and saved them against
// the wrong QSO. The fix keeps the auto-fill only while the radio is still
// in the bucket the spot came from.
//
// This test pins the rule that decision rests on: SpotIndex::bucketHz(),
// the frequency half of a bucket, shared by the index and by MainWindow so
// "same place" means one thing in both.
//
// ⭐ The cases that matter most:
//   • a phone wiggle inside the 1 kHz step stays in the same bucket;
//   • a QSY to the next channel does NOT;
//   • CW's 100 Hz step is tighter, so a 400 Hz move already leaves it.

#include "SpotIndex.h"

#include <QString>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

void checkEq(qint64 got, qint64 want, const char* what)
{
    const bool ok = (got == want);
    std::printf("%s  %s (got %lld, want %lld)\n",
                ok ? "PASS" : "FAIL", what,
                static_cast<long long>(got), static_cast<long long>(want));
    if (!ok) ++failures;
}

} // namespace

int main()
{
    // ── the step each mode buckets by ────────────────────────────────────
    checkEq(SpotIndex::stepHzForMode("SSB"), 1000, "SSB steps 1 kHz");
    checkEq(SpotIndex::stepHzForMode("CW"),   100,  "CW steps 100 Hz");
    checkEq(SpotIndex::stepHzForMode("FT8"),  100,  "FT8 steps 100 Hz");

    // ── phone: a wiggle around the spot keeps the same bucket ────────────
    const qint64 ssbSpot = SpotIndex::bucketHz(14.250000, "SSB");
    checkEq(ssbSpot, 14250000, "14.250 SSB rounds to 14250000");
    check(SpotIndex::bucketHz(14.250400, "SSB") == ssbSpot,
          "a 400 Hz phone wiggle up stays in the spot's bucket");
    check(SpotIndex::bucketHz(14.249600, "SSB") == ssbSpot,
          "a 400 Hz phone wiggle down stays in the spot's bucket");

    // ── phone: a real QSY leaves it — this is the bug's case ─────────────
    check(SpotIndex::bucketHz(14.252000, "SSB") != ssbSpot,
          "a 2 kHz QSY leaves the bucket (stale auto-fill must be dropped)");
    check(SpotIndex::bucketHz(14.300000, "SSB") != ssbSpot,
          "a 50 kHz QSY leaves the bucket");

    // ── rounding is to NEAREST, not floor (matches roundedHz's comment) ──
    checkEq(SpotIndex::bucketHz(14.250500, "SSB"), 14251000,
            "14.250500 SSB rounds up to the next channel, not down");

    // ── CW's tighter step: 400 Hz is already a different station ─────────
    const qint64 cwSpot = SpotIndex::bucketHz(7.030000, "CW");
    checkEq(cwSpot, 7030000, "7.030 CW rounds to 7030000");
    check(SpotIndex::bucketHz(7.030040, "CW") == cwSpot,
          "a 40 Hz CW wiggle stays in the bucket");
    check(SpotIndex::bucketHz(7.030400, "CW") != cwSpot,
          "a 400 Hz CW move leaves the bucket (tighter than phone)");

    // ── a mode with no step must not claim bucket 0 == bucket 0 ──────────
    // An empty mode yields the phone default rather than 0, so callers get a
    // real bucket; tryAutofillFromSpot() refuses to run on an empty mode
    // anyway. Pin it so a future change to stepHzForMode is noticed here.
    checkEq(SpotIndex::stepHzForMode(""), 1000, "an unknown mode falls back to 1 kHz");

    // ── the index agrees with the helper: a spot found at the wiggle ─────
    SpotIndex idx;
    SpotData s;
    s.call     = "G0JKN";
    s.freqMhz  = 14.250000;
    s.mode     = "SSB";
    s.comment  = "POTA K-1234";
    s.source   = "test";
    idx.addOrUpdate(s);

    check(idx.findAt(14.250400, "SSB").has_value(),
          "index finds the spot from inside the bucket");
    check(!idx.findAt(14.252000, "SSB").has_value(),
          "index finds nothing two channels away — the no-hit path the fix guards");

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
