#ifndef FILERECEIVER_H
#define FILERECEIVER_H

#include <QFile>
#include <QMap>
#include <QMutex>
#include <QObject>

/**
 * @brief 接收方接收文件管理类
 */

struct ReceivingFileInfo {
    QString fileId;
    QString fileName;
    QString savePath;                   // 最终保存路径
    QString tempPath;                   // 临时文件路径
    qint64 totalSize = 0;
    qint64 receivedSize = 0;
    int totalChunks = 0;
    int receivedChunks = 0;             // 接收到第几片
    QFile *file = nullptr;
    QMap<int, bool> receivedChunkMap;   // 记录已接收的分片
    int senderId = 0;                   // 发送者ID（用于解密）
    QString expectedMD5;                // 期望的MD5值（用于验证）
};

class FileReceiver : public QObject
{
    Q_OBJECT
public:
    static FileReceiver &instance();

    // 设置当前用户ID（用于解密）
    void setCurrentUserId(int userId) { m_currentUserId = userId; }

    // 开始接收文件
    bool startReceiving(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId = 0, const QString &expectedMD5 = QString());

    // 接收分片数据
    bool receiveChunk(const QString &fileId,int chunkIndex,const QByteArray &data);

    // 取消接收文件
    void cancelReceiving(const QString &fileId);

    // 暂停接收文件
    void pauseReceiving(const QString &fileId);

    // 获取接收任务
    ReceivingFileInfo *getReceivingInfo(const QString &fileId);

    // 获取已完成分片的位图（用于断点续传响应）
    QByteArray getCompletedChunksBitmap(const QString &fileId);

    // 验证文件MD5
    bool verifyFileMD5(const QString &filePath, const QString &expectedMD5);
private:
    // 重命名临时文件为最终文件
    void renameTempFile(const QString &fileId);
    
    // 内部实现函数（在锁保护下调用）
    bool startReceivingInternal(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId, const QString &expectedMD5);
    
    explicit FileReceiver(QObject *parent = nullptr);
    ~FileReceiver();

    QMap<QString, ReceivingFileInfo*> m_receivingFiles;
    QMutex m_mutex;             // 保护并发访问
    int m_currentUserId = 0;    // 当前用户ID（用于解密）
signals:

    // 接收进度信号
    void receiveProgress(const QString &fileId, int percent, qint64 received, qint64 total);

    // 接收成功信号
    void receiveCompleted(const QString &fileId,const QString &savePath);

    // 接收失败信号
    void receiveFailed(const QString &fileId, const QString &error);
};

#endif // FILERECEIVER_H
