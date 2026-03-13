#ifndef RECONNECTMANAGER_H
#define RECONNECTMANAGER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>

/**
 * @brief Reconnect manager.
 * Responsibilities:
 * 1. Detect disconnections.
 * 2. Auto reconnect (exponential backoff).
 * 3. Save login state and auto-login after reconnect.
 * 4. Track reconnect attempts and delay.
 */
class ReconnectManager : public QObject
{
    Q_OBJECT
public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting
    };

    explicit ReconnectManager(QObject *parent = nullptr);

    void setConnectionState(ConnectionState state);
    ConnectionState getConnectState() const { return m_state; }

    bool isReconnecting() const { return m_state == Reconnecting; }

    void setAutoConnect(bool enable);
    void startReconnect();
    void stopReconnect();
    void onReconnectSuccess();

    int getReconnectAttempts() const { return m_reconnectAttempts; }

    void setServerInfo(const QString &ip, uint16_t port);
    QString getServerIp() const { return m_serverIp; }
    uint16_t getServerPort() const { return m_serverPort; }

    // Save hashed credential (not plaintext password).
    void saveLoginInfo(const QString &userName, const QString &credentialHash);
    void clearLoginInfo();
    bool hasLoginInfo() const { return !m_savedUserName.isEmpty() && !m_savedCredentialCipher.isEmpty(); }
    QString getSavedUsername() const { return m_savedUserName; }

    void setInitialDelay(int ms);
    void setMaxDelay(int ms);
    void setMaxAttempts(int count);

private slots:
    void doReconnect();

signals:
    void needAutoLogin(const QString &userName, const QString &credentialHash);
    void reconnectStateChanged(int attempts, int delayMs);
    void maxAttemptsReached();
    void needReconnect(const QString &ip, uint16_t port);

private:
    int calculateNextDelay();
    QByteArray xorWithKey(const QByteArray &data, const QByteArray &key) const;
    QString decryptSavedCredential() const;

private:
    ConnectionState m_state;
    bool m_autoReconnect;
    int m_reconnectAttempts;

    QString m_serverIp;
    uint16_t m_serverPort;

    QString m_savedUserName;
    QByteArray m_savedCredentialCipher;
    QByteArray m_credentialKey;

    int m_initialDelay;
    int m_maxDelay;
    int m_maxAttempts;

    QTimer *m_reconnectTimer;
};

#endif // RECONNECTMANAGER_H
