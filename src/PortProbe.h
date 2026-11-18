#pragma once

#include <QHash>
#include <QObject>
#include <QSet>

// Asks published ports whether they speak HTTP, so the Docker panel links
// only web servers and not Postgres or Redis. One HEAD request per port, with
// a short timeout; the answer is kept until the port is gone.
class PortProbe : public QObject
{
    Q_OBJECT
public:
    explicit PortProbe(QObject *parent = nullptr);

    // Probes the ports not asked about yet and forgets the ones not listed.
    void setPorts(const QSet<int> &ports);
    // true: answered HTTP; false: did not (or not yet known).
    bool isHttp(int port) const { return m_http.value(port, false); }
    bool known(int port) const { return m_http.contains(port); }

    static constexpr int kTimeoutMs = 500;

Q_SIGNALS:
    void changed();

private:
    void probe(int port);

    QHash<int, bool> m_http;
    QSet<int> m_pending;
};
