// Drive RigctldClient against an EXTERNAL rigctld and print what it sees.
//
// Not a unit test — a bench tool. rigctld_client_test.cpp uses fakes written in
// the same codebase as the client, so a shared wrong assumption would pass
// both. This points the real client at something written independently (the
// Python personas in shack-experiments/kpa1500/dual_amp_simulator, or a REAL
// rigctld driving a real radio) and reports what actually came back.
//
//   rigctld_live_probe [host] [port] [seconds]
//   rigctld_live_probe 127.0.0.1 4532 5
//
// ⚠ Against a real radio this only ever READS. RigctldClient has no PTT and no
// set-frequency, by construction.

#include "RigctldClient.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

using namespace ShackBook;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString host = argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("127.0.0.1");
    const quint16 port = argc > 2 ? quint16(QString::fromUtf8(argv[2]).toUInt())
                                  : RigctldClient::defaultPort();
    const int secs = argc > 3 ? QString::fromUtf8(argv[3]).toInt() : 5;

    std::printf("probing rigctld at %s:%u for %d s\n\n",
                qUtf8Printable(host), port, secs);

    RigctldClient client;
    client.setPollIntervalMs(250);

    QObject::connect(&client, &RigctldClient::connectionChanged, [](bool c) {
        std::printf("  connection : %s\n", c ? "UP" : "down");
    });
    QObject::connect(&client, &RigctldClient::modelNameChanged, [](const QString& m) {
        std::printf("  model      : %s\n", m.isEmpty() ? "(cleared)" : qUtf8Printable(m));
    });
    QObject::connect(&client, &RigctldClient::frequencyChanged, [](double mhz) {
        std::printf("  frequency  : %.6f MHz\n", mhz);
    });
    QObject::connect(&client, &RigctldClient::modeChanged, [](const QString& m) {
        std::printf("  mode       : %s\n", qUtf8Printable(m));
    });

    client.connectToServer(host, port);

    QTimer::singleShot(secs * 1000, &app, [&]() {
        std::printf("\nfinal state: connected=%s model=%s freq=%.6f MHz mode=%s\n",
                    client.connected() ? "yes" : "no",
                    qUtf8Printable(client.modelName()),
                    client.currentFrequencyMhz(),
                    qUtf8Printable(client.currentMode()));
        const bool usable = client.connected()
                            && client.currentFrequencyMhz() > 0.0
                            && !client.modelName().isEmpty();
        std::printf("%s\n", usable ? "USABLE — radio followed successfully"
                                   : "NOT USABLE — see above");
        app.exit(usable ? 0 : 1);
    });

    return app.exec();
}
