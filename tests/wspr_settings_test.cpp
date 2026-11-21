// wspr_settings_test — the WSPR feed's settings round-trip, defaults and limits.
//
// #42 shipped a WSPR feed whose settings nothing in the app could write. The
// Settings dialog and MainWindow now share loadWsprConfig / saveWsprConfig;
// this checks those against a plain map, without a database.

#include "WsprSettings.h"

#include <QMap>
#include <QString>

#include <cstdio>

using namespace ShackBook;

namespace {
int g_failures = 0;
void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Store {
    QMap<QString, QString> values;
    QMap<QString, QString> writes;     // what the last save wrote
    WsprSettingGetter get() {
        return [this](const QString& k, const QString& d) { return values.value(k, d); };
    }
    WsprSettingSetter set() {
        return [this](const QString& k, const QString& v) { values[k] = v; writes[k] = v; };
    }
};
} // namespace

int main()
{
    // An empty log: off, no host, the agent's port, once a minute.
    {
        Store s;
        const WsprConfig c = loadWsprConfig(s.get());
        check(!c.enabled, "absent WSPR_ENABLE means off");
        check(c.host.isEmpty(), "absent WSPR_HOST means no host");
        check(c.port == 48995, "absent WSPR_PORT means 48995");
        check(c.pollSec == 60, "absent WSPR_POLL_SEC means 60 s");
        check(!c.runnable(), "an empty log does not run the feed");
    }

    // What Settings saves is what MainWindow loads.
    {
        Store s;
        WsprConfig c;
        c.enabled = true;
        c.host    = QStringLiteral("  192.168.0.47 ");
        c.port    = 48996;
        c.pollSec = 120;
        saveWsprConfig(c, s.get(), s.set());
        const WsprConfig back = loadWsprConfig(s.get());
        check(back.enabled && back.host == QStringLiteral("192.168.0.47")
                  && back.port == 48996 && back.pollSec == 120,
              "save then load round-trips, host trimmed");
        check(s.values.value("WSPR_ENABLE") == "1" && s.values.value("WSPR_HOST") == "192.168.0.47",
              "the keys WsprClient's caller reads are the ones written");
        check(back.runnable(), "on with a host runs the feed");
    }

    // On without a host does not run (MainWindow says so in the cluster log).
    {
        Store s;
        s.values["WSPR_ENABLE"] = "1";
        s.values["WSPR_HOST"]   = "   ";
        const WsprConfig c = loadWsprConfig(s.get());
        check(c.enabled && c.host.isEmpty() && !c.runnable(),
              "on with a blank host is enabled but not runnable");
    }

    // Limits on load: a hand-edited or old value cannot break the poller.
    {
        Store s;
        s.values["WSPR_PORT"] = "0";
        s.values["WSPR_POLL_SEC"] = "3";
        WsprConfig c = loadWsprConfig(s.get());
        check(c.port == 48995, "port 0 loads as 48995");
        check(c.pollSec == 10, "a 3 s poll is raised to 10 s");
        s.values["WSPR_PORT"] = "70000";
        s.values["WSPR_POLL_SEC"] = "99999";
        c = loadWsprConfig(s.get());
        check(c.port == 48995, "port 70000 loads as 48995");
        check(c.pollSec == 3600, "a 99999 s poll is capped at 3600 s");
        s.values["WSPR_POLL_SEC"] = "soon";
        c = loadWsprConfig(s.get());
        check(c.pollSec == 60, "an unreadable poll interval loads as 60 s");
    }

    // An OK in Settings with nothing changed writes nothing.
    {
        Store s;
        s.values["WSPR_ENABLE"] = "1";
        s.values["WSPR_HOST"]   = "radio.local";
        const WsprConfig c = loadWsprConfig(s.get());
        saveWsprConfig(c, s.get(), s.set());
        check(s.writes.isEmpty(), "saving an unchanged config writes no keys");
        WsprConfig off = c;
        off.enabled = false;
        saveWsprConfig(off, s.get(), s.set());
        check(s.writes.size() == 1 && s.writes.value("WSPR_ENABLE") == "0",
              "turning it off writes only WSPR_ENABLE");
        check(s.values.value("WSPR_HOST") == "radio.local",
              "turning it off keeps the host for next time");
    }

    std::printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
