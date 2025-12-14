#include "filetransferthread.h"
#include "logger.h"
#include "transferstatemanager.h"

#include <QCryptographicHash>
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
    , m_paused(false)
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
    m_paused = false;
}

void FileTransferThread::setCompletedChunks(const QSet<int> &completedChunks)
{
    QMutexLocker locker(&m_mutex);
    m_completedChunks = completedChunks;

    if(!m_completedChunks.isEmpty()){
        LOG_INFO_FMT("Resume transfer from chunk %1, total completed: %2", *m_completedChunks.begin(), m_completedChunks.size());
    }
}

void FileTransferThread::pauseTransfer()
{
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    LOG_INFO("Transfer paused by user");
}

void FileTransferThread::resumeTransfer()
{
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    LOG_INFO("Transfer resumed by user");
}

QString FileTransferThread::calculateFileMD5()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_FMT("Failed to open file for MD5 calculation: %1", m_filePath);
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);

    // 分块读取计算MD5，避免大文件内存溢出
    const int bufferSize = 1024 * 1024; // 1MB
    while (!file.atEnd()) {
        QByteArray buffer = file.read(bufferSize);
        hash.addData(buffer);
    }

    file.close();
    QString md5 = QString(hash.result().toHex());
    LOG_INFO_FMT("File MD5 calculated: %1", md5);
    return md5;
}

void FileTransferThread::run()
{
    QFile file(m_filePath);

    // 打开文件前检查是否已停止
    {
        QMutexLocker locker(&m_mutex);
        if(m_stopped){
            LOG_WARN("Transfer stopped before start");
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

    // 计算已传输的大小
    quint64 totalSent = 0;
    int stateChunkIndex = 0;

    {
        QMutexLocker locker(&m_mutex);
        totalSent = m_completedChunks.size() * m_chunkSize;

        // 如果有断点传输，找到第一个未完成的分片
        if(!m_completedChunks.isEmpty()){
        for (int i = 0; i < totalChunks; ++i) {
            if (!m_completedChunks.contains(i)) {
                stateChunkIndex = i;
                break;
            }
        }
        LOG_INFO_FMT("Resuming from chunk %1/%2", stateChunkIndex, totalSent);
    }
    }

    // 从断点开始传输
    for (int i = stateChunkIndex; i < totalChunks; ++i) {
        // 检查是否需要停止传输文件
        {
            QMutexLocker locker(&m_mutex);
            if(m_stopped){
                LOG_WARN("Transfer stopped by user");
                break;
            }

            // 检查是否需要暂停传输文件
            while(m_paused && !m_stopped){
                LOG_INFO("Transfer paused by user");
                msleep(100);
                locker.relock();
            }

            if(m_stopped){
                break;
            }

            // 跳过已完成的分片
            if( m_completedChunks.contains(i)){
                continue;
            }
        }

        // 定位到对应分片位置
        quint64 offset = static_cast<quint64>(i) * m_chunkSize;
        if(!file.seek(offset)){
            emit transferFailed("无法定位到分片位置："+file.errorString());
            break;
        }

        // 读取一个分片
        QByteArray chunk = file.read(m_chunkSize);
        if(chunk.isEmpty()){
            break;
        }

        totalSent += chunk.size();
        int percent = fileSize > 0 ? (totalSent * 100) / fileSize : 0;

        // 发送分片信号（会自动跨线程传递到主线程）
        emit chunkReady(chunk,i,totalChunks);

        // 发送进度更新信号
        emit progressUpdated(percent,totalSent,fileSize);

        // 标记已传输的分片
        {
            QMutexLocker locker(&m_mutex);
            m_completedChunks.insert(i);
        }

        // 标记分片已完成（每次都要更新内存状态，内部会每10个分片才持久化一次）
        TransferStateManager::instance().markChunkCompleted(m_fileId, i);

        // 延迟10ms发送下一片避免网络拥塞
        msleep(10);
    }

    file.close();

    // 检查是否被中断
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopped) {
            emit transferFailed("传输已取消");
            return;
        }

        if(m_paused){
            emit transferPaused(m_completedChunks.size());
            return;
        }
    }

    // 传输完成，清理状态
    TransferStateManager::instance().removeTransferState(m_fileId);
    emit transferCompleted();
}








