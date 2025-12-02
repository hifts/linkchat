#ifndef FILERECEIVER_H
#define FILERECEIVER_H

#include <QFile>
#include <QMap>
#include <QMutex>
#include <QObject>

// 接收方接收文件类

struct ReceivingFileInfo {
    QString fileId;
    QString fileName;
    QString savePath;
    qint64 totalSize;
    qint64 receivedSize;
    int totalChunks;
    int receivedChunks;                 // 接收到第几片
    QFile *file;
    QMap<int, bool> receivedChunkMap;   // 记录已接收的分片
};

class FileReceiver : public QObject
{
    Q_OBJECT
public:
    static FileReceiver &instance();

    // 开始接收文件
    bool startReceiving(const QString &fileId, const QString &fileName, qint64 fileSize);

    // 接收分片数据
    bool receiveChunk(const QString &fileId,int chunkIndex,const QByteArray &data);

    // 取消接收文件
    void cancelReceiving(const QString &fileId);

    // 获取接收任务
    ReceivingFileInfo *getReceivingInfo(const QString &fileId);
private:
    explicit FileReceiver(QObject *parent = nullptr);
    ~FileReceiver();

    QMap<QString, ReceivingFileInfo*> m_receivingFiles;
    QMutex m_mutex;     // 保护并发访问
signals:

    // 接收进度信号
    void receiveProgress(const QString &fileId, int percent, qint64 received, qint64 total);

    // 接收成功信号
    void receiveCompleted(const QString &fileId,const QString &savePath);

    // 接收失败信号
    void receiveFailed(const QString &fileId, const QString &error);
};

#endif // FILERECEIVER_H
