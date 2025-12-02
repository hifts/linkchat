#include "filetransferthread.h"

#include <QFile>
#include <qdebug.h>

FileTransferThread::FileTransferThread(const QString &filePath,
                                       const QString &fileId,
                                       int friendId,
                                       QObject *parent)
    : QThread(parent)
    , m_filePath(filePath)
    , m_fileId(fileId)
    , m_friendId(friendId)
    , m_chunkSize(64 * 1024)  // 默认64KB每片
    , m_stopped(false)
{}

FileTransferThread::~FileTransferThread()
{
    // 确保线程安全退出
    stopTransfer();
    wait();
}

void FileTransferThread::setChunkSize(quint64 newChunkSize)
{
    m_chunkSize = newChunkSize;
}

void FileTransferThread::stopTransfer()
{
    QMutexLocker locker(&m_mutex);
    m_stopped = true;
}

void FileTransferThread::run()
{
    QFile file(m_filePath);

    // 打开文件前检查是否已停止
    {
        QMutexLocker locker(&m_mutex);
        if(m_stopped){
            qDebug() << "[FileTransferThread] Transfer stopped before start";
            return;
        }
    }

    // 打开文件
    if(!file.open(QIODevice::ReadOnly)){
        emit transferFailed("无法打开文件："+file.errorString());
        return;
    }

    // 计算总分片数
    qint64 fileSize = file.size();
    if(fileSize == 0){
        emit transferFailed("文件大小为0");
        file.close();
        return;
    }

    int totalChunks = (fileSize + m_chunkSize - 1) / m_chunkSize;

    // 总共传输的大小和第几片了
    qint64 totalSent = 0;
    int chunkIndex = 0;

    while (!file.atEnd()) {
        // 检查是否需要停止传输文件
        {
            QMutexLocker locker(&m_mutex);
            if(m_stopped){
                qDebug() << "[FileTransferThread] Transfer stopped by user";
                break;
            }
        }

        // 读取一个分片
        QByteArray chunk = file.read(m_chunkSize);
        if(chunk.isEmpty()){
            break;
        }

        totalSent += chunk.size();
        int percent = fileSize > 0 ? (totalSent * 100) / fileSize : 0;

        // 发送分片信号（会自动跨线程传递到主线程）
        emit chunkReady(chunk,chunkIndex,totalChunks);

        // 发送进度更新信号
        emit progressUpdated(percent,totalSent,fileSize);

        chunkIndex++;

        // 延迟10ms发送下一片避免网络拥塞
        msleep(10);
    }

    // 发完之后（或者停止传输之后）关闭文件
    file.close();

    // 检查是否被中断
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopped) {
            emit transferFailed("传输已取消");
            return;
        }
    }

    // 传输完成
    emit transferCompleted();
}








