#ifndef FILETRANSFERSESSION_H
#define FILETRANSFERSESSION_H

#include <QDateTime>
#include <QMap>
#include <QSet>

class FileTransferSession
{
public:
    enum class State {
        Idle,
        Sending,
        Paused,
        WaitingForAck,
        Completed,
        Failed,
        Canceled
    };

    struct ChunkRetry {
        int attempts = 0;
        qint64 lastSentMs = 0;
    };

    explicit FileTransferSession(int totalChunks = 0);

    void reset(int totalChunks, const QSet<int> &ackedChunks);
    void setState(State state);
    State state() const;

    QList<int> takeSendWindow(int maxInFlight);
    QList<int> timedOutChunks(qint64 nowMs, int timeoutMs, int maxAttempts) const;
    void markSent(int chunkIndex, qint64 nowMs);
    bool markAcked(int chunkIndex);
    void markFailed();
    void markCanceled();

    bool isAcked(int chunkIndex) const;
    bool isComplete() const;
    int ackedCount() const;
    int totalChunks() const;
    int inFlightCount() const;
    const QSet<int> &ackedChunks() const;

private:
    int nextUnsentChunk() const;

    int m_totalChunks = 0;
    State m_state = State::Idle;
    QSet<int> m_ackedChunks;
    QMap<int, ChunkRetry> m_inFlightChunks;
};

#endif // FILETRANSFERSESSION_H
