#include "packet.h"
#include "packetcodec.h"
#include "filetransferconstants.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QTcpSocket>
#include <cstring>
#include <cstdio>

namespace {

QByteArray passwordHashBase64(const QString& password, const QByteArray& salt)
{
    QByteArray input = password.toUtf8();
    input.append(salt);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256).toBase64();
}

void copyBytes(char* dest, size_t destSize, const QByteArray& value)
{
    std::memset(dest, 0, destSize);
    std::strncpy(dest, value.constData(), destSize - 1);
}

class TestClient : public QObject
{
public:
    TestClient(QString username, QString password, QObject* parent = nullptr)
        : QObject(parent)
        , m_username(std::move(username))
        , m_password(std::move(password))
        , m_codec(DEFAULT_MAX_NORMAL_PACKET_LEN, DEFAULT_MAX_FILE_PACKET_LEN)
    {
        connect(&m_socket, &QTcpSocket::connected, this, [this]() { m_connected = true; sendLoginSaltReq(); });
        connect(&m_socket, &QTcpSocket::disconnected, this, [this]() { m_connected = false; });
        connect(&m_socket, &QTcpSocket::readyRead, this, &TestClient::onReadyRead);
    }

    void connectToServer(const QString& host, quint16 port)
    {
        m_socket.connectToHost(host, port);
    }

    bool isLoggedIn() const { return m_userId > 0; }
    int userId() const { return m_userId; }
    int receivedChunks() const { return m_receivedChunks; }
    int ackReceived() const { return m_ackReceived; }
    int ackPacketsSent() const { return m_ackPacketsSent; }
    int ackPacketsReceived() const { return m_ackPacketsReceived; }
    int pendingAckCount() const { return m_pendingAckChunks.size(); }
    int fileWriteCalls() const { return m_fileWriteCalls; }
    void setAckBatchSize(int ackBatchSize) { m_ackBatchSize = qMax(1, ackBatchSize); }
    qint64 fileFlushDueMs() const { return m_fileFlushDueMs; }
    bool transferRequestReceived() const { return m_transferRequestReceived; }
    bool transferAccepted() const { return m_transferAccepted; }
    qint64 receivedBytes() const { return m_receivedBytes; }
    QByteArray receivedMd5() const { return m_receiveHash.result().toHex(); }
    QString lastFileId() const { return m_lastFileId; }

    void sendFileRequest(const QString& fileId, const QString& fileName, int targetId, qint64 fileSize, int totalChunks)
    {
        FileTransferReq req;
        std::memset(&req, 0, sizeof(req));
        copyBytes(req.fileId, sizeof(req.fileId), fileId.toLatin1());
        copyBytes(req.fileName, sizeof(req.fileName), fileName.toUtf8());
        req.fileSize = static_cast<quint64>(fileSize);
        req.totalChunks = static_cast<quint32>(totalChunks);
        sendPacket(MSG_FILE_TRANSFER_REQ, QByteArray(reinterpret_cast<const char*>(&req), sizeof(req)), 0, targetId);
    }

    void sendChunk(const QString& fileId, int targetId, int chunkIndex, const QByteArray& data)
    {
        FileChunk chunk;
        std::memset(&chunk, 0, sizeof(chunk));
        copyBytes(chunk.fileId, sizeof(chunk.fileId), fileId.toLatin1());
        chunk.chunkIndex = static_cast<quint32>(chunkIndex);
        chunk.chunkSize = static_cast<quint32>(data.size());

        const QByteArray packet = makePacketFromParts(MSG_FILE_CHUNK,
                                                      reinterpret_cast<const char*>(&chunk),
                                                      sizeof(chunk),
                                                      data,
                                                      0,
                                                      targetId);
        sendFilePacket(packet);
    }

    void flushFilePackets()
    {
        m_fileFlushQueued = false;
        if (m_fileWriteBuffer.isEmpty()) {
            return;
        }
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_socket.write(m_fileWriteBuffer);
            m_fileWriteCalls++;
        }
        m_fileWriteBuffer.clear();
    }

    bool fileFlushQueued() const
    {
        return m_fileFlushQueued;
    }

private:
    void sendPacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0)
    {
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_socket.write(makePacket(type, body, src, dest));
        }
    }

    void sendFilePacket(const QByteArray& packet)
    {
        if (packet.isEmpty() || m_socket.state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (m_fileWriteBuffer.isEmpty()) {
            m_fileWriteBuffer.reserve(FILE_TRANSFER_WRITE_BATCH_BYTES + packet.size());
        }
        m_fileWriteBuffer.append(packet);
        if (m_fileWriteBuffer.size() >= FILE_TRANSFER_WRITE_BATCH_BYTES) {
            flushFilePackets();
            return;
        }
        scheduleFileFlush();
    }

    bool takeFileFlushQueued()
    {
        const bool queued = m_fileFlushQueued;
        m_fileFlushQueued = false;
        return queued;
    }

    void scheduleFileFlush()
    {
        m_fileFlushQueued = true;
        m_fileFlushDueMs = QDateTime::currentMSecsSinceEpoch() + 2;
    }

    bool hasPendingFilePackets() const
    {
        return !m_fileWriteBuffer.isEmpty();
    }

    void sendLoginSaltReq()
    {
        LoginSaltReq req;
        std::memset(&req, 0, sizeof(req));
        copyBytes(req.userName, sizeof(req.userName), m_username.toUtf8());
        sendPacket(MSG_LOGIN_SALT_REQ, QByteArray(reinterpret_cast<const char*>(&req), sizeof(req)));
    }

    void sendLoginReq(const QByteArray& saltBase64)
    {
        LoginReq req;
        std::memset(&req, 0, sizeof(req));
        copyBytes(req.userName, sizeof(req.userName), m_username.toUtf8());
        const QByteArray salt = QByteArray::fromBase64(saltBase64);
        copyBytes(req.passwordHash, sizeof(req.passwordHash), passwordHashBase64(m_password, salt));
        sendPacket(MSG_LOGIN_REQ, QByteArray(reinterpret_cast<const char*>(&req), sizeof(req)));
    }

    void onReadyRead()
    {
        m_buffer.append(m_socket.readAll());
        while (true) {
            DecodedPacket packet;
            QString error;
            const PacketReadStatus status = m_codec.takeNextPacket(m_buffer, packet, &error);
            if (status == PacketReadStatus::NeedMoreData) {
                return;
            }
            if (status == PacketReadStatus::ProtocolError) {
                std::fprintf(stderr, "protocol error: %s\n", error.toUtf8().constData());
                m_socket.abort();
                return;
            }
            handlePacket(packet.header, packet.body);
        }
    }

    void handlePacket(const PDUHeader& header, const QByteArray& body)
    {
        switch (header.msg_type) {
        case MSG_LOGIN_SALT_RESP: {
            if (body.size() < static_cast<int>(sizeof(LoginSaltResp))) return;
            LoginSaltResp resp;
            std::memcpy(&resp, body.constData(), sizeof(resp));
            if (resp.result == 1) {
                sendLoginReq(QByteArray(resp.salt));
            }
            break;
        }
        case MSG_LOGIN_RESP: {
            if (body.size() < static_cast<int>(sizeof(LoginResp))) return;
            LoginResp resp;
            std::memcpy(&resp, body.constData(), sizeof(resp));
            if (resp.result == 1) {
                m_userId = resp.userId;
            }
            break;
        }
        case MSG_FILE_TRANSFER_REQ: {
            if (body.size() < static_cast<int>(sizeof(FileTransferReq))) return;
            FileTransferReq req;
            std::memcpy(&req, body.constData(), sizeof(req));
            m_lastFileId = QString::fromLatin1(req.fileId);
            m_transferRequestReceived = true;

            FileTransferResp resp;
            std::memset(&resp, 0, sizeof(resp));
            copyBytes(resp.fileId, sizeof(resp.fileId), m_lastFileId.toLatin1());
            resp.accepted = 1;
            sendPacket(MSG_FILE_TRANSFER_RESP, QByteArray(reinterpret_cast<const char*>(&resp), sizeof(resp)), 0, header.src_id);
            break;
        }
        case MSG_FILE_TRANSFER_RESP: {
            if (body.size() < static_cast<int>(sizeof(FileTransferResp))) return;
            FileTransferResp resp;
            std::memcpy(&resp, body.constData(), sizeof(resp));
            m_transferAccepted = resp.accepted == 1;
            break;
        }
        case MSG_FILE_CHUNK: {
            if (body.size() < static_cast<int>(sizeof(FileChunk))) return;
            FileChunk chunk;
            std::memcpy(&chunk, body.constData(), sizeof(chunk));
            const int dataSize = qMin<int>(chunk.chunkSize, body.size() - static_cast<int>(sizeof(FileChunk)));
            const QByteArray chunkData = body.mid(sizeof(FileChunk), dataSize);
            m_receiveHash.addData(chunkData);
            m_receivedBytes += chunkData.size();
            m_receivedChunks++;

            queueAck(chunk.fileId, chunk.chunkIndex, header.src_id);
            break;
        }
        case MSG_FILE_TRANSFER_ACK: {
            if (body.size() < static_cast<int>(sizeof(FileTransferAck))) return;
            if (body.size() >= static_cast<int>(sizeof(FileTransferAckBatchHeader))) {
                FileTransferAckBatchHeader batchHeader;
                std::memset(&batchHeader, 0, sizeof(batchHeader));
                std::memcpy(&batchHeader, body.constData(), sizeof(batchHeader));
                const int maxAckCount = (body.size() - static_cast<int>(sizeof(FileTransferAckBatchHeader))) / static_cast<int>(sizeof(quint32));
                m_ackReceived += qMin<int>(batchHeader.ackCount, maxAckCount);
            } else {
                m_ackReceived++;
            }
            m_ackPacketsReceived++;
            break;
        }
        default:
            break;
        }
    }

public:
    void flushPendingAcks()
    {
        if (m_pendingAckChunks.isEmpty()) {
            return;
        }

        FileTransferAckBatchHeader ackHeader;
        std::memset(&ackHeader, 0, sizeof(ackHeader));
        std::memcpy(ackHeader.latestAck.fileId, m_pendingAckFileId, sizeof(ackHeader.latestAck.fileId));
        ackHeader.latestAck.chunkIndex = m_pendingAckChunks.last();
        ackHeader.ackCount = static_cast<quint32>(m_pendingAckChunks.size());

        QByteArray body(reinterpret_cast<const char*>(&ackHeader), sizeof(ackHeader));
        for (quint32 chunkIndex : m_pendingAckChunks) {
            body.append(reinterpret_cast<const char*>(&chunkIndex), sizeof(chunkIndex));
        }

        sendPacket(MSG_FILE_TRANSFER_ACK, body, 0, m_pendingAckTargetId);
        m_ackPacketsSent++;
        m_pendingAckChunks.clear();
    }

private:
    void queueAck(const char *fileId, quint32 chunkIndex, int targetId)
    {
        if (!m_pendingAckChunks.isEmpty()
            && (std::memcmp(m_pendingAckFileId, fileId, sizeof(m_pendingAckFileId)) != 0
                || m_pendingAckTargetId != targetId)) {
            flushPendingAcks();
        }

        std::memcpy(m_pendingAckFileId, fileId, sizeof(m_pendingAckFileId));
        m_pendingAckTargetId = targetId;
        m_pendingAckChunks.append(chunkIndex);

        if (m_pendingAckChunks.size() >= m_ackBatchSize) {
            flushPendingAcks();
        }
    }

    QString m_username;
    QString m_password;
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QByteArray m_fileWriteBuffer;
    bool m_fileFlushQueued = false;
    qint64 m_fileFlushDueMs = 0;
    PacketCodec m_codec;
    bool m_connected = false;
    int m_userId = 0;
    int m_receivedChunks = 0;
    int m_ackReceived = 0;
    int m_ackPacketsSent = 0;
    int m_ackPacketsReceived = 0;
    int m_ackBatchSize = FILE_TRANSFER_ACK_BATCH_SIZE;
    int m_fileWriteCalls = 0;
    bool m_transferRequestReceived = false;
    bool m_transferAccepted = false;
    qint64 m_receivedBytes = 0;
    QCryptographicHash m_receiveHash{QCryptographicHash::Md5};
    QString m_lastFileId;
    char m_pendingAckFileId[64] = {};
    int m_pendingAckTargetId = 0;
    QList<quint32> m_pendingAckChunks;
};

bool waitUntil(QCoreApplication& app, int timeoutMs, const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        app.processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    }
    return predicate();
}

QByteArray makeChunk(qint64 offset, int size)
{
    QByteArray chunk;
    chunk.resize(size);
    for (int i = 0; i < size; ++i) {
        chunk[i] = static_cast<char>(((offset + i) * 31 + 7) & 0xff);
    }
    return chunk;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString host = "127.0.0.1";
    quint16 port = 8080;
    QString senderName = "bench_200001";
    QString receiverName = "bench_200002";
    qint64 fileSize = 256 * 1024;
    int chunkSize = FILE_TRANSFER_CHUNK_SIZE;
    int windowSize = FILE_TRANSFER_MAX_IN_FLIGHT_CHUNKS;
    int ackBatchSize = FILE_TRANSFER_ACK_BATCH_SIZE;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString key = args.at(i);
        const QString value = (i + 1 < args.size()) ? args.at(i + 1) : QString();
        if (key == "--host" && !value.isEmpty()) { host = value; i++; }
        else if (key == "--port" && !value.isEmpty()) { port = value.toUShort(); i++; }
        else if (key == "--sender" && !value.isEmpty()) { senderName = value; i++; }
        else if (key == "--receiver" && !value.isEmpty()) { receiverName = value; i++; }
        else if (key == "--file-size" && !value.isEmpty()) { fileSize = value.toLongLong(); i++; }
        else if (key == "--chunk-size" && !value.isEmpty()) { chunkSize = value.toInt(); i++; }
        else if (key == "--window" && !value.isEmpty()) { windowSize = value.toInt(); i++; }
        else if (key == "--ack-batch" && !value.isEmpty()) { ackBatchSize = qMax(1, value.toInt()); i++; }
    }

    TestClient sender(senderName, "123456");
    TestClient receiver(receiverName, "123456");
    receiver.setAckBatchSize(ackBatchSize);
    sender.connectToServer(host, port);
    receiver.connectToServer(host, port);

    if (!waitUntil(app, 10000, [&]() { return sender.isLoggedIn() && receiver.isLoggedIn(); })) {
        std::fprintf(stderr, "FAILED: clients did not login\n");
        return 1;
    }

    const QString fileId = QString("e2e_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const int totalChunks = static_cast<int>((fileSize + chunkSize - 1) / chunkSize);
    sender.sendFileRequest(fileId, "e2e_payload.bin", receiver.userId(), fileSize, totalChunks);

    if (!waitUntil(app, 10000, [&]() { return receiver.transferRequestReceived() && sender.transferAccepted(); })) {
        std::fprintf(stderr, "FAILED: file transfer handshake did not complete\n");
        return 1;
    }

    QCryptographicHash sourceHash(QCryptographicHash::Md5);
    int nextChunk = 0;
    QElapsedTimer transferTimer;
    transferTimer.start();
    const int transferTimeoutMs = qMax(30000, totalChunks * 100);
    while (sender.ackReceived() < totalChunks && transferTimer.elapsed() < transferTimeoutMs) {
        while (nextChunk < totalChunks && nextChunk - sender.ackReceived() < windowSize) {
            const qint64 offset = static_cast<qint64>(nextChunk) * chunkSize;
            const int currentSize = static_cast<int>(qMin<qint64>(chunkSize, fileSize - offset));
            const QByteArray chunk = makeChunk(offset, currentSize);
            sourceHash.addData(chunk);
            sender.sendChunk(fileId, receiver.userId(), nextChunk, chunk);
            ++nextChunk;
        }
        app.processEvents(QEventLoop::AllEvents, 50);
        if (sender.fileFlushQueued() && QDateTime::currentMSecsSinceEpoch() >= sender.fileFlushDueMs()) {
            sender.flushFilePackets();
        }
        if (nextChunk >= totalChunks && receiver.pendingAckCount() > 0) {
            receiver.flushPendingAcks();
        }
        QThread::msleep(1);
    }

    sender.flushFilePackets();
    receiver.flushPendingAcks();

    if (!waitUntil(app, 10000, [&]() {
            return receiver.receivedChunks() == totalChunks && sender.ackReceived() == totalChunks;
        })) {
        std::fprintf(stderr, "FAILED: chunks=%d/%d ack=%d/%d bytes=%lld/%lld\n",
                     receiver.receivedChunks(), totalChunks,
                     sender.ackReceived(), totalChunks,
                     static_cast<long long>(receiver.receivedBytes()), static_cast<long long>(fileSize));
        return 1;
    }

    const QByteArray srcMd5 = sourceHash.result().toHex();
    const QByteArray dstMd5 = receiver.receivedMd5();
    if (srcMd5 != dstMd5) {
        std::fprintf(stderr, "FAILED: md5 mismatch src=%s dst=%s\n", srcMd5.constData(), dstMd5.constData());
        return 1;
    }

    std::fprintf(stdout,
                 "FileTransferE2ETest passed sender=%d receiver=%d bytes=%lld chunks=%d ack=%d md5=%s\n",
                 sender.userId(), receiver.userId(), static_cast<long long>(fileSize), totalChunks, sender.ackReceived(), srcMd5.constData());
    std::fprintf(stdout,
                 "ack_packets_sent=%d ack_packets_received=%d file_write_calls=%d window=%d chunk_size=%d ack_batch=%d\n",
                 receiver.ackPacketsSent(), sender.ackPacketsReceived(), sender.fileWriteCalls(), windowSize, chunkSize, ackBatchSize);
    return 0;
}
