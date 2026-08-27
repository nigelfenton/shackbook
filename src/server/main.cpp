// shackbook-server — Phase 1a entry point.
//
// Pulls in LogbookModel from the desktop binary (reused, not rewritten —
// see design doc §16 reuse audit) and exposes it through:
//   * HTTP  on port 8080  — endpoints in src/server/HttpServer.cpp
//   * N3FJP on port 1100  — currently just <PROGRAM> + <APIVER>; FD-minimum
//                            command surface lands in Phase 1c.
//
// All three surfaces share a single Qt event loop on the main thread.
// LogbookModel signals are the change-notification mechanism that future
// WebSocket and N3FJP event push will both subscribe to.

#include "HttpServer.h"
#include "N3fjpServer.h"
#include "N3fjpClient.h"
#include "WsjtxAdifReceiver.h"
#include "LogbookModel.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("shackbook-server");
    QCoreApplication::setApplicationVersion("0.1.0-phase1c");
    QCoreApplication::setOrganizationName("G0JKN");

    QCommandLineParser cli;
    cli.setApplicationDescription("ShackBook server — web UI + N3FJP-compat API");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption httpPortOpt(
        QStringList{QStringLiteral("http-port"), QStringLiteral("p")},
        QStringLiteral("HTTP port (default 8080)"),
        QStringLiteral("port"), QStringLiteral("8080"));
    QCommandLineOption n3fjpPortOpt(
        QStringList{QStringLiteral("n3fjp-port")},
        QStringLiteral("N3FJP-compat TCP port (default 1100)"),
        QStringLiteral("port"), QStringLiteral("1100"));
    QCommandLineOption dbPathOpt(
        QStringList{QStringLiteral("db")},
        QStringLiteral("Path to the SQLite logbook (default: per-user data dir)"),
        QStringLiteral("path"));
    // Mirror mode: connect OUT to a remote N3FJP server and mirror its QSOs.
    QCommandLineOption n3fjpHostOpt(
        QStringList{QStringLiteral("n3fjp-host")},
        QStringLiteral("Mirror a remote N3FJP server: connect to HOST:n3fjp-client-port "
                       "and ingest its logged QSOs (e.g. a Field Day station laptop)."),
        QStringLiteral("host"));
    QCommandLineOption n3fjpClientPortOpt(
        QStringList{QStringLiteral("n3fjp-client-port")},
        QStringLiteral("Port of the remote N3FJP server to mirror (default 1100)."),
        QStringLiteral("port"), QStringLiteral("1100"));
    cli.addOption(n3fjpHostOpt);
    cli.addOption(n3fjpClientPortOpt);
    cli.addOption(httpPortOpt);
    cli.addOption(n3fjpPortOpt);
    cli.addOption(dbPathOpt);
    cli.process(app);

    const quint16 httpPort  = cli.value(httpPortOpt).toUShort();
    const quint16 n3fjpPort = cli.value(n3fjpPortOpt).toUShort();
    const QString dbPath    = cli.value(dbPathOpt);

    // Open the logbook (creates the file on first run).
    ShackBook::LogbookModel model;
    if (!model.open(dbPath)) {
        qCritical() << "logbook open failed:" << model.errorString();
        return 2;
    }
    qInfo().noquote() << "logbook open at" << model.databasePath()
                      << "(" << model.countQsos() << "QSOs)";

    using namespace ShackBook::Server;

    HttpServer http(&model);
    if (!http.start(httpPort)) return 1;

    N3fjpServer n3fjp(&model);
    if (!n3fjp.start(n3fjpPort)) return 1;

    // WSJT-X Secondary UDP Server receiver — shares port 1100 with the
    // N3FJP TCP server (TCP/UDP are independent sockets).  Bind failure
    // is non-fatal: log a warning but keep the rest of the server up.
    WsjtxAdifReceiver wsjtx(&model);
    if (!wsjtx.start(n3fjpPort)) {
        qWarning() << "WsjtxAdifReceiver did not start — WSJT-X UDP log path disabled";
    }

    // Optional mirror: pull a remote N3FJP server's QSOs into our logbook.
    N3fjpClient n3fjpClient(&model);
    const QString mirrorHost = cli.value(n3fjpHostOpt);
    if (!mirrorHost.isEmpty()) {
        const quint16 mirrorPort = cli.value(n3fjpClientPortOpt).toUShort();
        n3fjpClient.start(mirrorHost, mirrorPort);
        qInfo().noquote() << "  N3FJP MIRROR :" << QString("%1:%2 (ingesting its QSOs)")
                             .arg(mirrorHost).arg(mirrorPort);
    }

    qInfo() << "shackbook-server ready.";
    qInfo() << "  HTTP        :" << QString("http://localhost:%1").arg(httpPort);
    qInfo() << "  N3FJP TCP   :" << QString("nc localhost %1").arg(n3fjpPort);
    qInfo() << "  WSJT-X UDP  :" << QString("udp/%1 (Secondary UDP Server target)").arg(n3fjpPort);
    qInfo() << "Ctrl+C to stop.";

    return app.exec();
}
