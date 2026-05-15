#include "packet.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QTcpSocket>
#include <algorithm>
#include <cstring>

struct Options {
    QString host = "127.0.0.1";
    quint16 port = 8080;
    int clients = 100;
    int rampUpSeconds = 10;
    int heartbeatIntervalMs = 30000;
    int chatRate = 100;
    int reconnectAfterSeconds = 30;
    int durationSeconds = 0;
    int startIndex = 1;
    QString mode = "heartbeat";
    QString userPrefix = "load_";
    QString password = "123456";
    QString output;
};

struct Counters {
    int launched = 0;
    int connected = 0;
    int disconnected = 0;
    int loginOk = 0;
    int loginFailed = 0;
    int registerOk = 0;
    int registerFailed = 0;
    int heartbeatSent = 0;
    int heartbeatResp = 0;
    int chatSent = 0;
    int chatRecv = 0;
    QVector<qint64> rtts;
};

static Counters g;
static QVector<int> g_userIds;

static QByteArray passwordHashBase64(const QString& password, const QByteArray& salt)
{
    QByteArray input = password.toUtf8();
    input.append(salt);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256).toBase64();
}

class LoadClient : public QObject
{
public:
    explicit LoadClient(const Options& options, int index, QObject* parent = nullptr)
        : QObject(parent)
        , m_options(options)
        , m_index(index)
        , m_userName(QString("%1%2").arg(options.userPrefix).arg(options.startIndex + index, 6, 10, QLatin1Char('0')))
    {
        connect(&m_socket, &QTcpSocket::connected, this, &LoadClient::onConnected);
        connect(&m_socket, &QTcpSocket::disconnected, this, &LoadClient::onDisconnected);
        connect(&m_socket, &QTcpSocket::readyRead, this, &LoadClient::onReadyRead);
        connect(&m_heartbeatTimer, &QTimer::timeout, this, &LoadClient::sendHeartbeat);
        connect(&m_chatTimer, &QTimer::timeout, this, &LoadClient::sendChat);
    }

    void start()
    {
        if (m_stopping) {
            return;
        }
        m_socket.connectToHost(m_options.host, m_options.port);
    }

    void stop()
    {
        m_stopping = true;
        m_heartbeatTimer.stop();
        m_chatTimer.stop();
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_socket.disconnectFromHost();
        } else {
            m_socket.abort();
        }
    }

private:
    void onConnected()
    {
        if (m_stopping) {
            m_socket.disconnectFromHost();
            return;
        }
        if (!m_connected) {
            m_connected = true;
            g.connected++;
        }
        if (m_options.mode == "register") {
            sendRegister();
        } else if (m_options.mode == "login" || m_options.mode == "chat" || m_options.mode == "mixed" || m_options.mode == "reconnect") {
            sendLoginSaltReq();
        } else {
            startHeartbeat();
        }
    }

    void onDisconnected()
    {
        if (m_connected) {
            m_connected = false;
            g.connected--;
        }
        g.disconnected++;
        m_heartbeatTimer.stop();
        m_chatTimer.stop();

        if (m_options.mode == "reconnect" && !m_stopping) {
            QTimer::singleShot(1000, this, [this]() { start(); });
        }
    }

    void startHeartbeat()
    {
        sendHeartbeat();
        m_heartbeatTimer.start(m_options.heartbeatIntervalMs);
    }

    void sendPacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0)
    {
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_socket.write(makePacket(type, body, src, dest));
        }
    }

    void sendHeartbeat()
    {
        HeartbeatPacket hb;
        hb.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_lastHeartbeatMs = hb.timestamp;
        sendPacket(MSG_HEARTBEAT_REQ, QByteArray((char*)&hb, sizeof(hb)));
        g.heartbeatSent++;
    }

    void sendRegister()
    {
        RegisterReq req;
        memset(&req, 0, sizeof(req));
        const QByteArray salt = QString("salt_%1").arg(m_userName).toUtf8();
        strncpy(req.userName, m_userName.toUtf8().constData(), 31);
        strncpy(req.salt, salt.toBase64().constData(), 63);
        strncpy(req.passwordHash, passwordHashBase64(m_options.password, salt).constData(), 63);
        sendPacket(MSG_REGISTER_REQ, QByteArray((char*)&req, sizeof(req)));
    }

    void sendLoginSaltReq()
    {
        LoginSaltReq req;
        memset(&req, 0, sizeof(req));
        strncpy(req.userName, m_userName.toUtf8().constData(), 31);
        sendPacket(MSG_LOGIN_SALT_REQ, QByteArray((char*)&req, sizeof(req)));
    }

    void sendLoginReq(const QByteArray& saltBase64)
    {
        LoginReq req;
        memset(&req, 0, sizeof(req));
        strncpy(req.userName, m_userName.toUtf8().constData(), 31);
        const QByteArray salt = QByteArray::fromBase64(saltBase64);
        strncpy(req.passwordHash, passwordHashBase64(m_options.password, salt).constData(), 63);
        sendPacket(MSG_LOGIN_REQ, QByteArray((char*)&req, sizeof(req)));
    }

    void sendChat()
    {
        if (m_userId <= 0 || g_userIds.size() < 2) {
            return;
        }
        const int selfIndex = g_userIds.indexOf(m_userId);
        const int baseIndex = selfIndex >= 0 ? selfIndex : m_index;
        const int target = g_userIds.at((baseIndex + 1) % g_userIds.size());
        if (target == m_userId) {
            return;
        }
        QByteArray body;
        body.append(SUB_TEXT);
        body.append(QString("hello from %1 at %2").arg(m_userName).arg(QDateTime::currentMSecsSinceEpoch()).toUtf8());
        sendPacket(MSG_CHAT_TEXT, body, m_userId, target);
        g.chatSent++;
    }

    void onReadyRead()
    {
        m_buffer.append(m_socket.readAll());
        while (m_buffer.size() >= (int)sizeof(PDUHeader)) {
            PDUHeader header;
            memcpy(&header, m_buffer.constData(), sizeof(header));
            if (header.magic != PDU_MAGIC || header.total_len < sizeof(PDUHeader) || header.total_len > 1024 * 1024) {
                m_socket.abort();
                return;
            }
            if (m_buffer.size() < (int)header.total_len) {
                return;
            }
            const QByteArray body = m_buffer.mid(sizeof(PDUHeader), header.total_len - sizeof(PDUHeader));
            m_buffer.remove(0, header.total_len);
            handlePacket(header.msg_type, body);
        }
    }

    void handlePacket(uint32_t msgType, const QByteArray& body)
    {
        switch (msgType) {
        case MSG_HEARTBEAT_RESP:
            g.heartbeatResp++;
            if (m_lastHeartbeatMs > 0) {
                g.rtts.append(QDateTime::currentMSecsSinceEpoch() - m_lastHeartbeatMs);
            }
            break;
        case MSG_REGISTER_RESP: {
            if (body.size() < (int)sizeof(LoginResp)) break;
            LoginResp resp;
            memcpy(&resp, body.constData(), sizeof(resp));
            resp.result == 1 ? g.registerOk++ : g.registerFailed++;
            m_socket.disconnectFromHost();
            break;
        }
        case MSG_LOGIN_SALT_RESP: {
            if (body.size() < (int)sizeof(LoginSaltResp)) break;
            LoginSaltResp resp;
            memcpy(&resp, body.constData(), sizeof(resp));
            if (resp.result != 1) {
                g.loginFailed++;
                return;
            }
            sendLoginReq(QByteArray(resp.salt));
            break;
        }
        case MSG_LOGIN_RESP: {
            if (body.size() < (int)sizeof(LoginResp)) break;
            LoginResp resp;
            memcpy(&resp, body.constData(), sizeof(resp));
            if (resp.result == 1) {
                m_userId = resp.userId;
                if (!g_userIds.contains(m_userId)) {
                    g_userIds.append(m_userId);
                }
                g.loginOk++;
                startHeartbeat();
                if (m_options.mode == "chat" || m_options.mode == "mixed") {
                    const int interval = qMax(1, 1000 * qMax(1, m_options.clients) / qMax(1, m_options.chatRate));
                    m_chatTimer.start(interval);
                }
                if (m_options.mode == "mixed") {
                    sendPacket(MSG_FRIEND_LIST_REQ, QByteArray());
                }
                if (m_options.mode == "reconnect") {
                    QTimer::singleShot(m_options.reconnectAfterSeconds * 1000, this, [this]() {
                        if (!m_stopping) {
                            m_socket.abort();
                        }
                    });
                }
            } else {
                g.loginFailed++;
            }
            break;
        }
        case MSG_CHAT_TEXT:
            g.chatRecv++;
            break;
        default:
            break;
        }
    }

    Options m_options;
    int m_index = 0;
    int m_userId = 0;
    QString m_userName;
    QTcpSocket m_socket;
    QTimer m_heartbeatTimer;
    QTimer m_chatTimer;
    QByteArray m_buffer;
    bool m_connected = false;
    bool m_stopping = false;
    qint64 m_lastHeartbeatMs = 0;
};

static Options parseOptions(const QStringList& args)
{
    Options opt;
    for (int i = 1; i < args.size(); ++i) {
        const QString key = args[i];
        const QString value = (i + 1 < args.size()) ? args[i + 1] : QString();
        if (key == "--host" && !value.isEmpty()) { opt.host = value; i++; }
        else if (key == "--port" && !value.isEmpty()) { opt.port = value.toUShort(); i++; }
        else if (key == "--clients" && !value.isEmpty()) { opt.clients = value.toInt(); i++; }
        else if (key == "--ramp-up" && !value.isEmpty()) { opt.rampUpSeconds = value.toInt(); i++; }
        else if (key == "--heartbeat-interval" && !value.isEmpty()) { opt.heartbeatIntervalMs = value.toInt(); i++; }
        else if (key == "--chat-rate" && !value.isEmpty()) { opt.chatRate = value.toInt(); i++; }
        else if (key == "--reconnect-after" && !value.isEmpty()) { opt.reconnectAfterSeconds = value.toInt(); i++; }
        else if (key == "--duration" && !value.isEmpty()) { opt.durationSeconds = value.toInt(); i++; }
        else if (key == "--start-index" && !value.isEmpty()) { opt.startIndex = value.toInt(); i++; }
        else if (key == "--mode" && !value.isEmpty()) { opt.mode = value; i++; }
        else if (key == "--user-prefix" && !value.isEmpty()) { opt.userPrefix = value; i++; }
        else if (key == "--password" && !value.isEmpty()) { opt.password = value; i++; }
        else if (key == "--output" && !value.isEmpty()) { opt.output = value; i++; }
    }
    opt.clients = qMax(1, opt.clients);
    opt.rampUpSeconds = qMax(1, opt.rampUpSeconds);
    opt.heartbeatIntervalMs = qMax(1000, opt.heartbeatIntervalMs);
    opt.chatRate = qMax(1, opt.chatRate);
    opt.durationSeconds = qMax(0, opt.durationSeconds);
    opt.startIndex = qMax(1, opt.startIndex);
    return opt;
}

static qint64 percentile(QVector<qint64> values, double p)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const int idx = qBound(0, static_cast<int>((values.size() - 1) * p), values.size() - 1);
    return values[idx];
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const Options options = parseOptions(app.arguments());
    const QStringList modes = {"heartbeat", "register", "login", "chat", "mixed", "reconnect"};
    if (!modes.contains(options.mode)) {
        qCritical("Unsupported mode. Use heartbeat/register/login/chat/mixed/reconnect.");
        return 2;
    }

    QFile csv;
    QTextStream csvOut;
    if (!options.output.isEmpty()) {
        csv.setFileName(options.output);
        if (csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
            csvOut.setDevice(&csv);
            csvOut << "elapsed,launched,connected,disconnected,login_ok,login_failed,register_ok,register_failed,hb_sent,hb_resp,chat_sent,chat_recv,rtt_p50,rtt_p95,rtt_p99\n";
        }
    }

    QVector<LoadClient*> clients;
    clients.reserve(options.clients);
    const int intervalMs = qMax(1, options.rampUpSeconds * 1000 / options.clients);

    QTimer launcher;
    QObject::connect(&launcher, &QTimer::timeout, [&]() {
        if (g.launched >= options.clients) {
            launcher.stop();
            return;
        }
        auto* client = new LoadClient(options, g.launched, &app);
        clients.append(client);
        client->start();
        g.launched++;
    });
    launcher.start(intervalMs);

    QElapsedTimer elapsed;
    elapsed.start();
    QTimer reporter;
    QObject::connect(&reporter, &QTimer::timeout, [&]() {
        const QVector<qint64> rtts = g.rtts;
        const qint64 p50 = percentile(rtts, 0.50);
        const qint64 p95 = percentile(rtts, 0.95);
        const qint64 p99 = percentile(rtts, 0.99);
        const qint64 seconds = elapsed.elapsed() / 1000;
        qInfo("elapsed=%llds launched=%d connected=%d disconnected=%d login_ok=%d login_failed=%d register_ok=%d register_failed=%d hb_sent=%d hb_resp=%d chat_sent=%d chat_recv=%d rtt_p50=%lldms rtt_p95=%lldms rtt_p99=%lldms",
              seconds, g.launched, g.connected, g.disconnected, g.loginOk, g.loginFailed,
              g.registerOk, g.registerFailed, g.heartbeatSent, g.heartbeatResp, g.chatSent, g.chatRecv, p50, p95, p99);
        if (csvOut.device()) {
            csvOut << seconds << "," << g.launched << "," << g.connected << "," << g.disconnected << ","
                   << g.loginOk << "," << g.loginFailed << "," << g.registerOk << "," << g.registerFailed << ","
                   << g.heartbeatSent << "," << g.heartbeatResp << "," << g.chatSent << "," << g.chatRecv << ","
                   << p50 << "," << p95 << "," << p99 << "\n";
            csvOut.flush();
        }
    });
    reporter.start(5000);

    if (options.durationSeconds > 0) {
        QTimer::singleShot(options.durationSeconds * 1000, &app, [&app, &launcher, &reporter, &clients]() {
            qInfo("duration reached, stopping load tester");
            launcher.stop();
            reporter.stop();
            for (LoadClient* client : clients) {
                client->stop();
            }
            QTimer::singleShot(1000, &app, &QCoreApplication::quit);
        });
    }

    return app.exec();
}
