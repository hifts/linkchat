#ifndef FILETRANSFERTHREAD_H
#define FILETRANSFERTHREAD_H

#include <QMutex>
#include <QThread>
#include <QSet>

class FileTransferThread : public QThread
{
    Q_OBJECT
public:
    explicit FileTransferThread(const QString &filePath,
                                const QString &fileId,
                                int friendId,
                                QObject *parent = nullptr);

    ~FileTransferThread();

    // 设置分片大小
    void setChunkSize(quint64 newChunkSize);

    // 停止传输
    void stopTransfer();

    // 设置已完成的分片（用于断点传输）
    void setCompletedChunks(const QSet<int> &completedChunks);

    // 暂停传输
    void pauseTransfer();

    // 恢复传输
    void resumeTransfer();

    // 计算文件MD5
    QString calculateFileMD5();

signals:
    // 进度更新信号
    void progressUpdated(int percent, qint64 sent, qint64 total);

    // 分片准备好信号
    void chunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks);

    // 传输完成信号
    void transferCompleted();

    // 传输失败信号
    void transferFailed(const QString &error);

    // 传输暂停信号(暂停时的最后一个分片索引)
    void transferPaused(int lastChunkIndex);
protected:
    // 线程的主要工作在这里
    void run() override;

private:
    QString m_filePath;         // 文件路径
    QString m_fileId;           // 文件唯一标识
    int m_friendId;             // 好友ID
    quint64 m_chunkSize;        // 分片大小
    bool m_stopped;             // 是否停止传输文件
    bool m_paused;              // 是否暂停传输文件
    QMutex m_mutex;             // 互斥锁，保护m_stopped
    QSet<int> m_completedChunks;// 已完成的分片
};

#endif // FILETRANSFERTHREAD_H
