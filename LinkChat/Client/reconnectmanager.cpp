#include "reconnectmanager.h"

#include "configmanager.h"
#include "configkeys.h"
#include "logger.h"

#include <QRandomGenerator>
#include <QtMath>

ReconnectManager::ReconnectManager(QObject *parent)
    : QObject{parent}
    , m_state(Disconnected)
    , m_autoReconnect(true)
    , m_reconnectAttempts(0)
    , m_serverPort(0)
    , m_initialDelay(5000)
    , m_maxDelay(30000)
    , m_maxAttempts(5)
{
    m_autoReconnect = ConfigManager::instance().getBool(
        ConfigKeys::Client::AUTO_RECONNECT, true);
    m_maxAttempts = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_MAX_ATTEMPTS, 5);
    m_initialDelay = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_INTERVAL, 5000);
    m_maxDelay = m_initialDelay * 6;

    LOG_INFO(QString("Reconnect manager initialized: autoReconnect=%1, maxAttempts=%2, initialDelay=%3ms")
                 .arg(m_autoReconnect ? "enabled" : "disabled")
                 .arg(m_maxAttempts)
                 .arg(m_initialDelay));

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ReconnectManager::doReconnect);
}

void ReconnectManager::setConnectionState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;

    switch (state) {
    case Disconnected:
        LOG_INFO("State changed: disconnected");
        if (m_autoReconnect && !m_serverIp.isEmpty()) {
            startReconnect();
        }
        break;
    case Connecting:
        LOG_INFO("State changed: connecting");
        break;
    case Connected:
        LOG_INFO("State changed: connected");
        onReconnectSuccess();
        break;
    case Reconnecting:
        LOG_INFO("State changed: reconnecting");
        break;
    default:
        LOG_INFO("State changed: unknown");
        break;
    }
}

void ReconnectManager::setAutoConnect(bool enable)
{
    m_autoReconnect = enable;
    LOG_INFO(QString("Auto reconnect %1").arg(enable ? "enabled" : "disabled"));

    if (!enable) {
        stopReconnect();
    }
}

void ReconnectManager::startReconnect()
{
    if (!m_autoReconnect) {
        LOG_WARN("Auto reconnect is disabled, skip reconnect");
        return;
    }

    if (m_serverIp.isEmpty()) {
        LOG_ERROR("Server address is empty, cannot reconnect");
        return;
    }

    if (m_state == Reconnecting) {
        LOG_WARN("Already reconnecting, skip");
        return;
    }

    if (m_maxAttempts > 0 && m_reconnectAttempts >= m_maxAttempts) {
        LOG_ERROR(QString("Reached max reconnect attempts (%1), stop reconnecting").arg(m_maxAttempts));
        emit maxAttemptsReached();
        return;
    }

    m_state = Reconnecting;
    const int delay = calculateNextDelay();
    emit reconnectStateChanged(m_reconnectAttempts + 1, delay);
    m_reconnectTimer->start(delay);
}

void ReconnectManager::stopReconnect()
{
    if (m_reconnectTimer->isActive()) {
        LOG_INFO("Stop reconnect timer");
        m_reconnectTimer->stop();
    }

    if (m_state == Reconnecting) {
        m_state = Disconnected;
    }
}

void ReconnectManager::onReconnectSuccess()
{
    m_reconnectAttempts = 0;
    stopReconnect();

    if (hasLoginInfo()) {
        emit needAutoLogin(m_savedUserName, decryptSavedCredential());
    }
}

void ReconnectManager::setServerInfo(const QString &ip, uint16_t port)
{
    m_serverIp = ip;
    m_serverPort = port;
}

void ReconnectManager::saveLoginInfo(const QString &userName, const QString &credentialHash)
{
    m_savedUserName = userName;

    m_credentialKey.clear();
    m_credentialKey.resize(32);
    for (int i = 0; i < m_credentialKey.size(); ++i) {
        m_credentialKey[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    m_savedCredentialCipher = xorWithKey(credentialHash.toUtf8(), m_credentialKey);
}

void ReconnectManager::clearLoginInfo()
{
    m_savedUserName.clear();
    m_savedCredentialCipher.clear();
    m_credentialKey.clear();
}

void ReconnectManager::doReconnect()
{
    m_reconnectAttempts++;
    emit needReconnect(m_serverIp, m_serverPort);
}

int ReconnectManager::calculateNextDelay()
{
    if (m_reconnectAttempts == 0) {
        return m_initialDelay;
    }

    const int exponentialDelay = m_initialDelay * qPow(2, m_reconnectAttempts - 1);
    return qMin(exponentialDelay, m_maxDelay);
}

void ReconnectManager::setInitialDelay(int ms)
{
    m_initialDelay = ms;
}

void ReconnectManager::setMaxDelay(int ms)
{
    m_maxDelay = ms;
}

void ReconnectManager::setMaxAttempts(int count)
{
    m_maxAttempts = count;
}

QByteArray ReconnectManager::xorWithKey(const QByteArray &data, const QByteArray &key) const
{
    if (data.isEmpty() || key.isEmpty()) {
        return QByteArray();
    }

    QByteArray result(data.size(), '\0');
    for (int i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ key[i % key.size()];
    }
    return result;
}

QString ReconnectManager::decryptSavedCredential() const
{
    if (m_savedCredentialCipher.isEmpty() || m_credentialKey.isEmpty()) {
        return QString();
    }
    return QString::fromUtf8(xorWithKey(m_savedCredentialCipher, m_credentialKey));
}
