#pragma once

// N1mmSpotClient — listen for N1MM+ / DXLog bandmap spots over UDP (#11).
//
// A third spot source beside the DX cluster and POTA. During a contest it is
// the better one, for two reasons:
//   • it is filtered by the operator's own log, so spots carry contest state a
//     cluster cannot (dupe, mult, new, bust, ...);
//   • it says explicitly when a spot is gone, so the index stays accurate
//     rather than merely recent.
//
// ⛔ RECEIVE-ONLY. It binds and reads; it never sends a datagram.
//
// ⚠ The port is bound exclusively, on purpose. SmartSDR CAT and other N1MM
// consumers commonly hold 12060 already; with a shared bind the operating
// system would hand each spot to just one of the listeners, so ShackBook would
// see some spots and silently miss others. An exclusive bind fails loudly
// instead, and the fix is easy: pick another port here and add it to N1MM's
// list of broadcast destinations.

#include "SpotData.h"

#include <QObject>
#include <QString>

class QUdpSocket;

namespace ShackBook {

class N1mmSpotClient : public QObject {
    Q_OBJECT

public:
    explicit N1mmSpotClient(QObject* parent = nullptr);
    ~N1mmSpotClient() override;

    static quint16 defaultPort() { return 12060; }

    // Start listening on `port` on every interface (multi-op loggers are often
    // on other PCs). Returns false, with lastError() set, if the port is taken.
    bool start(quint16 port = defaultPort());
    void stop();

    bool    listening() const;
    quint16 port()      const { return m_port; }
    QString lastError() const { return m_lastError; }

    // Running counts since start(), for the diagnostics log.
    int spotsReceived() const { return m_spots; }
    int malformed()     const { return m_malformed; }

signals:
    void spotReceived(const ShackBook::SpotData& spot);
    // The logger removed this call's spot from its bandmap on this frequency's band.
    void spotDeleted(const QString& call, double freqMhz);
    void listeningChanged(bool listening, const QString& errorOrEmpty);

private:
    void onReadyRead();

    QUdpSocket* m_socket{nullptr};
    quint16     m_port{0};
    QString     m_lastError;
    int         m_spots{0};
    int         m_malformed{0};
};

} // namespace ShackBook
