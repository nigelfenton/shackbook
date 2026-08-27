#pragma once

// PotaClient — periodic poller for the POTA (Parks On The Air) public
// spot feed at https://api.pota.app/spot/activator.
//
// Returns a JSON array; each element is a current activator spot with
// fields like activator, frequency (kHz), mode, reference, parkName,
// locationDesc, comments, spotTime, expire, etc.  We translate each into
// a SpotData and emit it for SpotIndex to absorb.
//
// Anonymous, no authentication, no rate limits beyond reasonable use.
// Default poll interval 30 s — POTA spotters usually update once per
// minute or so during normal activity.

#include "SpotData.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace ShackBook {

class PotaClient : public QObject {
    Q_OBJECT
public:
    explicit PotaClient(QObject* parent = nullptr);
    ~PotaClient() override;

    // Start periodic polling.  pollSec ≥ 5; values smaller than 5 are
    // clamped to discourage abuse of the public endpoint.
    void start(int pollSec = 30);
    void stop();

    bool active() const;
    QString lastError() const { return m_lastError; }

signals:
    void spotReceived(const ShackBook::SpotData& spot);
    void pollCompleted(int spots, const QString& errorOrEmpty);

private slots:
    void poll();
    void onReplyFinished();

private:
    QNetworkAccessManager* m_nam{nullptr};
    QTimer*                m_pollTimer{nullptr};
    QNetworkReply*         m_inFlight{nullptr};
    QUrl     m_url{QStringLiteral("https://api.pota.app/spot/activator")};
    QString  m_lastError;
};

} // namespace ShackBook
