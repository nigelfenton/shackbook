#pragma once

// WsprClient — periodic poller for an AetherWSPR agent's /v1/status.
//
// AetherWSPR (AetherSDR PR #7019) is a receive-only WSPR decoder that runs
// ON the radio — a FLEX-6000/8000's embedded Linux — with wsprd compiled in.
// No PC is involved in the decode. It serves JSON over HTTP on port 48995:
//
//   {"agent":"AetherWSPR","state":"capturing","clock_source":"radio_ntp",
//    "band":"40m","dial_hz":7038600,
//    "spots":[{"t":1794148800,"band":"40m","snr":-21,"dt":-0.4,
//              "hz":7040000,"drift":0,"call":"W4VIR","grid":"EM72","dbm":37}]}
//
// Each spot becomes a SpotData for SpotIndex, so WSPR stations appear in the
// live spot panel beside DX cluster and POTA. WSPR is the most sensitive
// common mode by a wide margin, so it reveals which paths are open well
// before anything audible does.
//
// WHY THIS IS A SPOT FEED AND NOT LOG INGEST
// WSPR spots are one-way beacon receptions, not QSOs. Routing them through
// SpotIndex (which is memory-only, with a lifetime per spot) keeps them out
// of the logbook, where they would otherwise be counted by awardsSummary()
// toward DXCC, WAS and grids — contacts that never happened. Persisting them
// is a separate decision and deliberately not done here.
//
// NOTES ON THE AGENT'S JSON
//   * `spots` is a ring buffer of up to 2000 entries, newest last. Every poll
//     re-sends the whole window, so this client tracks the newest timestamp it
//     has emitted and only forwards spots past it. SpotIndex de-duplicates by
//     callsign anyway, but this keeps a restart from replaying hours of spots.
//   * `call` CAN BE EMPTY: WSPR type-2/3 messages carry a 6-character grid and
//     a hashed callsign that is never transmitted. Those spots are skipped,
//     since a spot panel keyed on callsign has nothing to show for them.
//   * `snr` is the WSPR SNR in dB (typically -31..0, negative is normal), not
//     an S-meter reading.
//   * `hz` is the spot's own frequency within the 200 Hz WSPR window, which is
//     NOT the dial frequency (`dial_hz` sits ~1500 Hz below it).

#include "SpotData.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace ShackBook {

class WsprClient : public QObject {
    Q_OBJECT
public:
    explicit WsprClient(QObject* parent = nullptr);
    ~WsprClient() override;

    // Point at the agent. Host may be a name or an address; the port is the
    // agent's HTTP port (48995 by default).
    void setAgent(const QString& host, int port = 48995);

    // Start periodic polling. A WSPR cycle is 120 s, so polling faster than
    // that cannot yield new spots; values below 10 s are clamped to protect
    // the radio's single ARM core, which is also running the decoder.
    void start(int pollSec = 60);
    void stop();

    bool active() const;
    QString lastError() const { return m_lastError; }

    // Last reported agent state ("capturing", "waiting_clock", ...) and the
    // clock source it is using ("kernel", "radio_ntp", "none"), for the
    // status bar. Empty until the first successful poll.
    QString agentState() const { return m_agentState; }
    QString clockSource() const { return m_clockSource; }

signals:
    void spotReceived(const ShackBook::SpotData& spot);
    // spots = number forwarded this poll (new ones only, not the window size).
    void pollCompleted(int spots, const QString& errorOrEmpty);
    // Emitted when the agent's reported state changes, so the UI can say when
    // the receiver is stalled rather than silently showing no spots.
    void agentStateChanged(const QString& state, const QString& clockSource);

private slots:
    void poll();
    void onReplyFinished();

private:
    QNetworkAccessManager* m_nam{nullptr};
    QTimer*                m_pollTimer{nullptr};
    QNetworkReply*         m_inFlight{nullptr};
    QUrl     m_url;
    QString  m_lastError;
    QString  m_agentState;
    QString  m_clockSource;
    // Newest spot timestamp already emitted (Unix seconds, UTC). 0 = none yet.
    qint64   m_lastSpotT{0};
    int      m_pollSec{60};
};

} // namespace ShackBook
