#include "filetransfersession.h"

FileTransferSession::FileTransferSession(int totalChunks)
    : m_totalChunks(totalChunks)
{}

void FileTransferSession::reset(int totalChunks, const QSet<int> &ackedChunks)
{
    m_totalChunks = totalChunks;
    m_ackedChunks = ackedChunks;
    m_inFlightChunks.clear();
    m_state = State::Sending;
}

void FileTransferSession::setState(State state)
{
    m_state = state;
}

FileTransferSession::State FileTransferSession::state() const
{
    return m_state;
}

QList<int> FileTransferSession::takeSendWindow(int maxInFlight)
{
    QList<int> chunks;
    if (m_state != State::Sending && m_state != State::WaitingForAck) {
        return chunks;
    }

    while (m_inFlightChunks.size() < maxInFlight) {
        const int chunkIndex = nextUnsentChunk();
        if (chunkIndex < 0) {
            break;
        }
        chunks.append(chunkIndex);
        m_inFlightChunks.insert(chunkIndex, ChunkRetry());
    }

    if (chunks.isEmpty() && !isComplete()) {
        m_state = State::WaitingForAck;
    }
    return chunks;
}

QList<int> FileTransferSession::timedOutChunks(qint64 nowMs, int timeoutMs, int maxAttempts) const
{
    QList<int> chunks;
    for (auto it = m_inFlightChunks.cbegin(); it != m_inFlightChunks.cend(); ++it) {
        if (it.value().attempts >= maxAttempts) {
            continue;
        }
        if (nowMs - it.value().lastSentMs >= timeoutMs) {
            chunks.append(it.key());
        }
    }
    return chunks;
}

void FileTransferSession::markSent(int chunkIndex, qint64 nowMs)
{
    ChunkRetry retry = m_inFlightChunks.value(chunkIndex);
    retry.attempts++;
    retry.lastSentMs = nowMs;
    m_inFlightChunks[chunkIndex] = retry;
    if (m_state == State::Idle) {
        m_state = State::Sending;
    }
}

bool FileTransferSession::markAcked(int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_totalChunks || m_ackedChunks.contains(chunkIndex)) {
        return false;
    }

    m_inFlightChunks.remove(chunkIndex);
    m_ackedChunks.insert(chunkIndex);
    if (isComplete()) {
        m_state = State::Completed;
    } else {
        m_state = State::Sending;
    }
    return true;
}

void FileTransferSession::markFailed()
{
    m_state = State::Failed;
}

void FileTransferSession::markCanceled()
{
    m_state = State::Canceled;
    m_inFlightChunks.clear();
}

bool FileTransferSession::isAcked(int chunkIndex) const
{
    return m_ackedChunks.contains(chunkIndex);
}

bool FileTransferSession::isComplete() const
{
    return m_totalChunks > 0 && m_ackedChunks.size() >= m_totalChunks;
}

int FileTransferSession::ackedCount() const
{
    return m_ackedChunks.size();
}

int FileTransferSession::totalChunks() const
{
    return m_totalChunks;
}

int FileTransferSession::inFlightCount() const
{
    return m_inFlightChunks.size();
}

const QSet<int> &FileTransferSession::ackedChunks() const
{
    return m_ackedChunks;
}

int FileTransferSession::nextUnsentChunk() const
{
    for (int i = 0; i < m_totalChunks; ++i) {
        if (!m_ackedChunks.contains(i) && !m_inFlightChunks.contains(i)) {
            return i;
        }
    }
    return -1;
}
