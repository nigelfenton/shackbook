#include "HamlibRigList.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace HamlibRigList {

QString Rig::label() const
{
    return QStringLiteral("%1 %2 (%3)").arg(manufacturer, name).arg(model);
}

QList<Rig> parseRigctlList(const QString& output)
{
    QList<Rig> rigs;
    // Two-or-more spaces. Single spaces are INSIDE the fields -- "NET rigctl",
    // "Vertex Standard", "Ten-Tec" -- so splitting on one space would cut a
    // manufacturer in half and shift every column after it.
    static const QRegularExpression columnGap(QStringLiteral("\\s{2,}"));

    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const QStringList cols = line.split(columnGap, Qt::SkipEmptyParts);
        // Rig #, Mfg, Model, Version, Status, Macro. Anything shorter is the
        // header, a banner line, or a warning on stderr that got merged in.
        if (cols.size() < 3)
            continue;
        bool ok = false;
        const int model = cols.at(0).toInt(&ok);
        // The header row's first column is "Rig #", which is not a number --
        // so this check drops it without needing to match its text.
        if (!ok || model <= 0)
            continue;
        rigs.append(Rig{model, cols.at(1), cols.at(2)});
    }
    return rigs;
}

QList<Rig> fromInstalledHamlib(const QString& rigctldPath)
{
    if (rigctldPath.isEmpty())
        return {};
    // rigctl lives beside rigctld in every layout we look in; it is the one
    // that prints the list. Deriving it from the path we already resolved
    // avoids a second search that could disagree with the first.
    const QFileInfo info(rigctldPath);
    QString rigctl = info.absolutePath() + QStringLiteral("/rigctl");
#ifdef Q_OS_WIN
    rigctl += QStringLiteral(".exe");
#endif
    if (!QFileInfo::exists(rigctl))
        return {};

    QProcess proc;
    proc.start(rigctl, {QStringLiteral("-l")});
    // Bounded: a hung helper must not hang the settings dialog. Listing rigs
    // is a table print with no I/O, so a second is already generous.
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        proc.waitForFinished(500);
        return {};
    }
    return parseRigctlList(QString::fromUtf8(proc.readAllStandardOutput()));
}

QList<Rig> bundledFallback()
{
    // A SMALL, DELIBERATELY PARTIAL LIST.
    //
    // Enough for a new operator to recognise their radio and see what the
    // field wants before Hamlib is installed. It is never preferred over the
    // installed Hamlib's own list: a bundled table drifts, and offering a
    // model number the local Hamlib does not have would be worse than
    // offering nothing. Numbers below are from Hamlib 4.7.2.
    return {
        {1035, QStringLiteral("Yaesu"),   QStringLiteral("FT-991")},
        {1042, QStringLiteral("Yaesu"),   QStringLiteral("FTDX-10")},
        {1040, QStringLiteral("Yaesu"),   QStringLiteral("FT-710")},
        {2037, QStringLiteral("Kenwood"), QStringLiteral("TS-2000")},
        {2043, QStringLiteral("Kenwood"), QStringLiteral("TS-590S")},
        {2050, QStringLiteral("Kenwood"), QStringLiteral("TS-890S")},
        {3073, QStringLiteral("Icom"),    QStringLiteral("IC-7300")},
        {3078, QStringLiteral("Icom"),    QStringLiteral("IC-7610")},
        {3081, QStringLiteral("Icom"),    QStringLiteral("IC-9700")},
        {3085, QStringLiteral("Icom"),    QStringLiteral("IC-705")},
        {2,    QStringLiteral("Hamlib"),  QStringLiteral("NET rigctl")},
        {1,    QStringLiteral("Hamlib"),  QStringLiteral("Dummy")},
    };
}

QList<Rig> best(const QString& rigctldPath, bool* usedFallback)
{
    const QList<Rig> installed = fromInstalledHamlib(rigctldPath);
    if (!installed.isEmpty()) {
        if (usedFallback)
            *usedFallback = false;
        return installed;
    }
    if (usedFallback)
        *usedFallback = true;
    return bundledFallback();
}

}  // namespace HamlibRigList
