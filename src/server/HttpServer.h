#pragma once

// Phase-1a HTTP server for shackbook-server.
//
// Built on plain QTcpServer with a tiny request parser — no QHttpServer, no
// cpp-httplib.  This is deliberate for the spike: prove three-surface
// coexistence (HTTP / N3FJP / Qt event loop) without adding a dependency
// that may or may not be available on every build host.  Later phases will
// swap this out for either QHttpServer (if installed) or cpp-httplib.
//
// Endpoints (Phase 1a):
//   GET /                 -> 200, "ShackBook Server alive\n"
//   GET /api/state        -> 200, JSON with live server stats incl. QSO count
//   GET /api/qsos         -> 200, JSON array of recent QSOs (limit query param)
//   anything else         -> 404
//
// Connections are short-lived: one request, one response, close.  HTTP/1.1
// keep-alive is not honoured.  Sufficient until we add the WebSocket fanout.

#include <QObject>
#include <QTcpServer>

namespace ShackBook {
class LogbookModel;
}

namespace ShackBook::Server {

class HttpServer : public QObject {
    Q_OBJECT

public:
    explicit HttpServer(LogbookModel* model, QObject* parent = nullptr);

    // Start listening on the given port.  Returns false on bind failure
    // (port in use, permission denied, etc.).
    bool start(quint16 port = 8080);

    quint16 port() const;
    bool    isListening() const;

private slots:
    void onNewConnection();

private:
    LogbookModel* m_model;     // not owned
    QTcpServer*   m_server;
};

} // namespace ShackBook::Server
