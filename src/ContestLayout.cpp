#include "ContestLayout.h"

#include <QStringList>

namespace ShackBook {

namespace {

// Exchange fields that live in the free-text exchange box. RST and serial
// have boxes of their own.
bool isTextField(ExchangeField f)
{
    return f != ExchangeField::Rst && f != ExchangeField::Serial;
}

// "COUNTY", or "NAME/STATE" when a contest exchanges two text items.
QString textLabel(const QVector<ExchangeField>& fields)
{
    QStringList names;
    for (ExchangeField f : fields)
        if (isTextField(f)) names << ContestCatalog::fieldName(f).toUpper();
    return names.join(QLatin1Char('/'));
}

void appendOnce(QVector<EntrySlot>& v, EntrySlot s)
{
    if (!v.contains(s)) v.append(s);
}

} // namespace

ContestLayout contestLayoutFor(const ContestDef& def)
{
    ContestLayout l;
    if (!def.isValid()) return l;
    l.contestId = def.id;
    l.name = def.name.isEmpty() ? def.id : def.name;

    QVector<EntrySlot> received;
    for (ExchangeField f : def.received) {
        if (f == ExchangeField::Rst) appendOnce(received, EntrySlot::RstRcvd);
        else if (f == ExchangeField::Serial) appendOnce(received, EntrySlot::Srx);
        else appendOnce(received, EntrySlot::SrxText);
    }
    QVector<EntrySlot> sent;
    for (ExchangeField f : def.sent) {
        if (f == ExchangeField::Rst) appendOnce(sent, EntrySlot::RstSent);
        else if (f == ExchangeField::Serial) appendOnce(sent, EntrySlot::Stx);
        else appendOnce(sent, EntrySlot::StxText);
    }

    l.tabOrder << EntrySlot::Call;
    for (EntrySlot s : received) appendOnce(l.tabOrder, s);
    for (EntrySlot s : sent) appendOnce(l.tabOrder, s);

    for (EntrySlot s : {EntrySlot::Call, EntrySlot::RstSent, EntrySlot::RstRcvd,
                        EntrySlot::Comment, EntrySlot::Stx, EntrySlot::StxText,
                        EntrySlot::Srx, EntrySlot::SrxText}) {
        if (!l.tabOrder.contains(s)) l.deemphasised.insert(s);
    }

    l.stxTextLabel = textLabel(def.sent);
    l.srxTextLabel = textLabel(def.received);
    l.requiredOnSave = received;
    return l;
}

QVector<EntrySlot> missingOnSave(const ContestLayout& layout,
                                 const QHash<EntrySlot, QString>& values)
{
    QVector<EntrySlot> missing;
    for (EntrySlot s : layout.requiredOnSave)
        if (values.value(s).trimmed().isEmpty()) missing << s;
    return missing;
}

QString entrySlotName(const ContestLayout& layout, EntrySlot slot)
{
    auto titled = [](const QString& upper) {
        QString t = upper.toLower();
        if (!t.isEmpty()) t[0] = t[0].toUpper();
        return t;
    };
    switch (slot) {
    case EntrySlot::Call:    return QStringLiteral("Call");
    case EntrySlot::RstSent: return QStringLiteral("RST sent");
    case EntrySlot::RstRcvd: return QStringLiteral("RST received");
    case EntrySlot::Comment: return QStringLiteral("Comment");
    case EntrySlot::Stx:     return QStringLiteral("Serial sent");
    case EntrySlot::Srx:     return QStringLiteral("Serial received");
    case EntrySlot::StxText:
        return layout.stxTextLabel.isEmpty() ? QStringLiteral("Exchange sent")
                                             : titled(layout.stxTextLabel) + QStringLiteral(" sent");
    case EntrySlot::SrxText:
        return layout.srxTextLabel.isEmpty() ? QStringLiteral("Exchange received")
                                             : titled(layout.srxTextLabel);
    }
    return {};
}

} // namespace ShackBook
