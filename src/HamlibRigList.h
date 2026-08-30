#pragma once

// The Hamlib rig catalogue, so the operator picks "Icom IC-9700" instead of
// having to discover that it is model number 3081.
//
// `rigctld -m <model>` wants a number, and `rigctl -l` prints 314 of them on
// Hamlib 4.7.2. Nothing in ShackBook used to help find it, and it is not
// guessable — which is most of why setting up a non-TCI radio was a research
// task rather than a settings change. See issue #13.

#include <QList>
#include <QString>

namespace HamlibRigList {

struct Rig {
    int     model = 0;      // the number `rigctld -m` wants
    QString manufacturer;   // "Icom"
    QString name;           // "IC-9700"

    // "Icom IC-9700 (3081)" — what the picker shows and what search matches.
    [[nodiscard]] QString label() const;
};

// Parse the output of `rigctl -l`. Exposed for testing: it is the half that
// can go wrong, and it must not need Hamlib present to be exercised.
//
// The table is fixed-column with a header line, e.g.
//
//   Rig #  Mfg          Model        Version      Status      Macro
//       1  Hamlib       Dummy        20240709.0   Stable      RIG_MODEL_DUMMY
//
// Manufacturer and model names both contain spaces ("Ten-Tec", "IC-9700 (Net)"),
// so this splits on runs of two-or-more spaces rather than on single ones.
[[nodiscard]] QList<Rig> parseRigctlList(const QString& output);

// Every rig the INSTALLED Hamlib knows, by running `rigctl -l` next to the
// given rigctld path. Empty when Hamlib is absent or the call fails.
[[nodiscard]] QList<Rig> fromInstalledHamlib(const QString& rigctldPath);

// A small built-in list so the picker is useful BEFORE Hamlib is installed.
//
// Deliberately partial, and deliberately never preferred: when Hamlib is
// present its own list wins, because a bundled list can drift and offer a
// model number the installed Hamlib does not have. This exists so a new
// operator can see what the field wants, not as a substitute catalogue.
[[nodiscard]] QList<Rig> bundledFallback();

// The list to show: the installed Hamlib's if we have it, otherwise the
// fallback. `usedFallback` reports which, so the UI can say so plainly rather
// than presenting a partial list as if it were complete.
[[nodiscard]] QList<Rig> best(const QString& rigctldPath, bool* usedFallback = nullptr);

}  // namespace HamlibRigList
