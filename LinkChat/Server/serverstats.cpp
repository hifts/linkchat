#include "serverstats.h"
#include "logger.h"

ServerStats& ServerStats::instance()
{
    static ServerStats stats;
    return stats;
}

ServerStats::ServerStats(QObject *parent)
    : QObject(parent)
{
}

void ServerStats::start(int intervalMs)
{
    if (!m_reportTimer) {
        m_reportTimer = new QTimer(this);
        connect(m_reportTimer, &QTimer::timeout, this, &ServerStats::report);
    }
    m_reportTimer->start(intervalMs);
}

void ServerStats::connectionAccepted() { m_currentConnections++; m_totalAccepted++; }
void ServerStats::connectionRejected() { m_totalRejected++; }
void ServerStats::connectionClosed() { if (m_currentConnections > 0) m_currentConnections--; m_totalClosed++; }
void ServerStats::userLoggedIn() { m_currentUsers++; }
void ServerStats::userLoggedOut() { if (m_currentUsers > 0) m_currentUsers--; }
void ServerStats::packetReceived() { m_totalPacketsIn++; }
void ServerStats::packetSent() { m_totalPacketsOut++; }
void ServerStats::heartbeatReceived() { m_totalHeartbeats++; }
void ServerStats::loginSucceeded() { m_totalLoginSuccess++; }
void ServerStats::loginFailed() { m_totalLoginFailed++; }
void ServerStats::messageHandled(uint32_t msgType) { Q_UNUSED(msgType); }
void ServerStats::dbTaskQueued() { m_pendingDbTasks++; }
void ServerStats::dbTaskStarted() {}
void ServerStats::dbTaskFinished(qint64 elapsedMs)
{
    if (m_pendingDbTasks > 0) {
        m_pendingDbTasks--;
    }
    m_totalDbTasks++;
    m_totalDbTimeMs += static_cast<quint64>(qMax<qint64>(0, elapsedMs));
}

int ServerStats::currentConnections() const { return m_currentConnections.load(); }
int ServerStats::currentUsers() const { return m_currentUsers.load(); }
int ServerStats::pendingDbTasks() const { return m_pendingDbTasks.load(); }

void ServerStats::report()
{
    const quint64 packetsIn = m_totalPacketsIn.load();
    const quint64 packetsOut = m_totalPacketsOut.load();
    const quint64 heartbeats = m_totalHeartbeats.load();
    const quint64 loginSuccess = m_totalLoginSuccess.load();
    const quint64 loginFailed = m_totalLoginFailed.load();
    const quint64 dbTasks = m_totalDbTasks.load();
    const quint64 dbTime = m_totalDbTimeMs.load();

    const quint64 deltaDb = dbTasks - m_lastDbTasks;
    const double avgDbMs = dbTasks == 0 ? 0.0 : static_cast<double>(dbTime) / static_cast<double>(dbTasks);

    LOG_WARN(QString("[Stats] conn=%1 users=%2 accepted=%3 rejected=%4 closed=%5 "
                     "pps_in=%6 pps_out=%7 heartbeat_per_5s=%8 login_ok_5s=%9 login_fail_5s=%10 db_pending=%11 db_done_5s=%12 db_avg_ms=%13")
                 .arg(m_currentConnections.load())
                 .arg(m_currentUsers.load())
                 .arg(m_totalAccepted.load())
                 .arg(m_totalRejected.load())
                 .arg(m_totalClosed.load())
                 .arg(packetsIn - m_lastPacketsIn)
                 .arg(packetsOut - m_lastPacketsOut)
                 .arg(heartbeats - m_lastHeartbeats)
                 .arg(loginSuccess - m_lastLoginSuccess)
                 .arg(loginFailed - m_lastLoginFailed)
                 .arg(m_pendingDbTasks.load())
                 .arg(deltaDb)
                 .arg(avgDbMs, 0, 'f', 2));

    m_lastPacketsIn = packetsIn;
    m_lastPacketsOut = packetsOut;
    m_lastHeartbeats = heartbeats;
    m_lastLoginSuccess = loginSuccess;
    m_lastLoginFailed = loginFailed;
    m_lastDbTasks = dbTasks;
}
