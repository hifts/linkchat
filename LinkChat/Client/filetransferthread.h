#ifndef FILETRANSFERTHREAD_H
#define FILETRANSFERTHREAD_H

#include <QMutex>
#include <QThread>

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

signals:
    // 进度更新信号
    void progressUpdated(int percent, qint64 sent, qint64 total);

    // 分片准备好信号
    void chunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks);

    // 传输完成信号
    void transferCompleted();

    // 传输失败信号
    void transferFailed(const QString &error);

protected:
    // 线程的主要工作在这里
    void run() override;

private:
    QString m_filePath;         // 文件路径
    QString m_fileId;           // 文件唯一标识
    int m_friendId;             // 好友ID
    quint64 m_chunkSize;        // 分片大小
    bool m_stopped;             // 是否停止传输文件
    QMutex m_mutex;             // 互斥锁，保护m_stopped
};

#endif // FILETRANSFERTHREAD_H
