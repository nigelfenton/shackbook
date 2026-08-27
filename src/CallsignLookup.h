#pragma once

// CallsignLookup — asynchronous online callsign → operator-details lookup.
//
// Providers:
//   * QRZ XML  (xmldata.qrz.com)  — requires a paid QRZ XML subscription;
//     richest data (name, city, state, county, grid, DXCC).
//   * HamQTH   (hamqth.com)       — free account; name/QTH/grid/country and
//     the ADIF DXCC number.
//   * callook.info                — no account, US callsigns only; used as
//     an automatic fallback when no provider is configured and the call is
//     known (via cty.dat) to be a US station.
//
// Session keys are cached and re-acquired once on expiry. All lookups are
// non-blocking; results arrive via the result() signal and the UI merges
// them into EMPTY fields only — operator input is never overwritten.

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace ShackBook {

class CallsignLookup : public QObject {
    Q_OBJECT

public:
    enum class Provider { None, Qrz, HamQth };

    struct Result {
        QString call;
        QString name;
        QString qth;          // city
        QString state;
        QString county;
        QString country;
        QString grid;
        int     dxcc{0};
        QString source;       // "QRZ" / "HamQTH" / "callook"
        bool    ok{false};
        QString error;
    };

    explicit CallsignLookup(QObject* parent = nullptr);

    void configure(Provider provider, const QString& user,
                   const QString& password, bool callookForUs);

    Provider provider() const { return m_provider; }

    // Fire an async lookup. `isUsCall` is the cty.dat hint that selects the
    // callook fallback when no provider is configured.
    void lookup(const QString& call, bool isUsCall);

signals:
    void result(const ShackBook::CallsignLookup::Result& r);

private:
    void qrzLogin(const QString& pendingCall);
    void qrzQuery(const QString& call, bool retryOnExpiry);
    void hamqthLogin(const QString& pendingCall);
    void hamqthQuery(const QString& call, bool retryOnExpiry);
    void callookQuery(const QString& call);
    QNetworkReply* get(const QString& url);

    QNetworkAccessManager* m_nam{};
    Provider m_provider{Provider::None};
    QString  m_user, m_pass;
    bool     m_callookForUs{true};
    QString  m_qrzKey;          // session keys, cached until rejected
    QString  m_hamqthSession;
};

} // namespace ShackBook
