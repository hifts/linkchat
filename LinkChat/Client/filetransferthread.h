#ifndef FILETRANSFERTHREAD_H
#define FILETRANSFERTHREAD_H

#include <QMutex>
#include <QThread>
#include <QSet>
#include <QtGlobal>

class FileTransferThread : public QThread
{
    Q_OBJECT
public:
    explicit FileTransferThread(const QString &filePath,
                                const QString &fileId,
                                int friendId,
                                int currentUserId,
                                qint64 transferSize,
                                QObject *parent = nullptr);

    ~FileTransferThread();

    void setChunkSize(quint64 newChunkSize);

    void stopTransfer();

    void setCompletedChunks(const QSet<int> &completedChunks);

    QByteArray readEncryptedChunk(int chunkIndex, int *totalChunks = nullptr, QString *error = nullptr);

    void pauseTransfer();

    void resumeTransfer();

    QString calculateFileMD5();

signals:
    void progressUpdated(int percent, qint64 sent, qint64 total);

    void chunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks);

    void transferCompleted();

    void transferFailed(const QString &error);

    void transferPaused(int lastChunkIndex);
protected:
    void run() override;

private:
    QString m_filePath;
    QString m_fileId;
    int m_friendId;
    int m_currentUserId;
    qint64 m_transferSize;
    quint64 m_chunkSize;
    bool m_stopped;
    bool m_paused;
    QMutex m_mutex;
    QSet<int> m_completedChunks;
};

#endif // FILETRANSFERTHREAD_H
