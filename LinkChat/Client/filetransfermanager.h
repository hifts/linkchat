#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include "filetransferthread.h"

#include <QObject>
#include <QMap>

// 管理线程类(单例模式)

struct FileTransferInfo {
    QString fileId;                 // 文件唯一标识
    QString fileName;               // 文件名
    QString filePath;               // 文件路径
    qint64 fileSize;                // 文件大小
    int friendId;                   // 好友ID
    bool isReceiving;               // true=接收 false=发送
    int progress;                   // 传输进度
    FileTransferThread *thread;     // 线程类
};

class FileTransferManager : public QObject
{
    Q_OBJECT
public:
    static FileTransferManager &instance();

    // 开始发送文件
    QString startSendFile(const QString &fileId,const QString &filePath,int friendId);

    // 开始接受文件
    void startReceiveFile(const QString &fileId,const QString &fileName,qint64 fileSize,int friendId);

    // 取消传输
    void cancelTransfer(const QString &fileId);

    // 获取传输信息
    FileTransferInfo *getTransferInfo(const QString &fileId);

    // 文件ID
    QString generateFileId(const QString &filePath);
signals:
    // 传输文件信号
    void transferStarted(const QString &fileId, const QString &fileName);
    void transferProgress(const QString &fileId, int percent, qint64 sent, qint64 total);
    void transferCompleted(const QString &fileId);
    void transferFailed(const QString &fileId, const QString &error);

    // 发送分片信号（连接到NetworkManager）
    void sendFileChunk(const QString &fileId, const QByteArray &chunk,
                       int chunkIndex, int totalChunks, int friendId);

private slots:
    // 处理传输文件线程信号
    void onChunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks);
    void onProgressUpdated(int percent, qint64 sent, qint64 total);
    void onTransferCompleted();
    void onTransferFailed(const QString &error);

private:
    explicit FileTransferManager(QObject *parent = nullptr);
    ~FileTransferManager();



    QMap<QString,FileTransferInfo*> m_transfers;    // 存放文件传输信息包括传输线程类
};

#endif // FILETRANSFERMANAGER_H
