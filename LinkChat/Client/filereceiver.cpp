#include "filereceiver.h"
#include "transferstatemanager.h"
#include "logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <qdebug.h>

FileReceiver &FileReceiver::instance()
{
    static FileReceiver instance;
    return instance;
}

FileReceiver::FileReceiver(QObject *parent)
    : QObject{parent}
{}

FileReceiver::~FileReceiver()
{
    // 清理所有接收任务
    for (auto it = m_receivingFiles.begin(); it != m_receivingFiles.end(); ++it) {
        if (it.value()->file) {
            it.value()->file->close();
            delete it.value()->file;
        }
        delete it.value();
    }
    m_receivingFiles.clear();
}

bool FileReceiver::startReceiving(const QString &fileId, const QString &fileName, qint64 fileSize)
{
    QMutexLocker locker(&m_mutex);

    // 检查文件大小是否有效
    if(fileSize <= 0){
        LOG_ERROR_FMT("Invalid file size: %1", fileSize);
        emit receiveFailed(fileId, "文件大小无效");
        return false;
    }

    // 如果已经存在相同的fileId，先清理旧的
    if(m_receivingFiles.contains(fileId)){
        LOG_WARN_FMT("File receiving already exists for fileId: %1, cleaning up old one", fileId);
        ReceivingFileInfo *oldInfo = m_receivingFiles[fileId];
        if(oldInfo->file){
            oldInfo->file->close();
            delete oldInfo->file;
        }
        delete oldInfo;
        m_receivingFiles.remove(fileId);
    }

    // 检查是否存在断点续传状态
    TransferState saveState = TransferStateManager::instance().loadTransferState(fileId);

    QString savePath;
    QString tempPath;
    QFile *file = nullptr;
    ReceivingFileInfo *info = new ReceivingFileInfo;

    QString downloadPath = QCoreApplication::applicationDirPath() + "/ReceivedFiles";
    QDir().mkpath(downloadPath);

    if(!saveState.fileId.isEmpty() && !saveState.isSending && saveState.fileSize == fileSize){
        // 断点续传：检查临时文件是否存在
        tempPath = saveState.tempFilePath;
        savePath = saveState.filePath;

        if(!tempPath.isEmpty() && QFile::exists(tempPath)){
            file = new QFile(tempPath);
            if(!file->open(QIODevice::ReadWrite)){
                LOG_ERROR_FMT("Failed to open temp file:%1",tempPath);
                delete file;
                delete info;
                emit receiveFailed(fileId, "无法打开临时文件");
                return false;
            }

            // 恢复已接收的分片信息
            for(int chunk : saveState.completedChunks){
                info->receivedChunkMap[chunk] = true;
            }

            info->receivedChunks = saveState.completedChunks.size();
            info->receivedSize = saveState.completedChunks.size() * 64 * 1024;

            LOG_INFO(QString("Resuming file receiving:%1,completed chunks:%2/%3")
                         .arg(fileName).arg(info->receivedChunks).arg(saveState.totalChunks));
        } else {
            // 临时文件不存在，重新开始
            LOG_WARN_FMT("Temp file not found, starting fresh: %1", tempPath);
            saveState.fileId.clear(); // 清空以触发新建逻辑
            file = nullptr; // 确保file为nullptr，触发新建逻辑
        }
    }

    // 如果file仍然为nullptr，需要创建新文件
    if(saveState.fileId.isEmpty() || file == nullptr){
        // 新建文件接收
        savePath = downloadPath + "/" + fileName;

        // 如果最终文件存在，添加序号
        int count = 1;
        while (QFile::exists(savePath)) {
            QFileInfo fi(fileName);
            QString baseName = fi.completeBaseName();
            QString ext = fi.suffix();
            savePath = downloadPath + "/" + baseName + QString("(%1).").arg(count) + ext;
            count++;
        }

        // 使用.tmp扩展名创建临时文件
        tempPath = savePath + ".tmp";

        file = new QFile(tempPath);
        if(!file->open(QIODevice::WriteOnly)){
            LOG_ERROR_FMT("Failed to create temp file:%1",tempPath);
            delete file;
            delete info;
            emit receiveFailed(fileId, "无法创建临时文件");
            return false;
        }

        info->receivedChunks = 0;
        info->receivedSize = 0;

        LOG_INFO_FMT("Starting new file receiving:%1, temp file:%2", fileName, tempPath);
    }

    // 检查file是否有效
    if(file == nullptr){
        LOG_ERROR_FMT("File object is null for fileId: %1", fileId);
        delete info;
        emit receiveFailed(fileId, "无法创建文件对象");
        return false;
    }

    // 创建接收文件信息
    info->file = file;
    info->fileId = fileId;
    info->fileName = fileName;
    info->savePath = savePath;
    info->tempPath = tempPath;
    info->totalSize = fileSize;
    info->totalChunks = (fileSize + 64 * 1024 -1) / (64 * 1024);

    // 保存传输文件任务
    m_receivingFiles[fileId] = info;

    // 保存文件传输状态
    TransferState state;
    state.fileId = fileId;
    state.fileName = fileName;
    state.filePath = savePath;
    state.tempFilePath = tempPath;
    state.fileSize = fileSize;
    state.friendId = 0;         // 接收方暂不记录friendId
    state.totalChunks = info->totalChunks;
    state.completedChunks = QSet<int>(info->receivedChunkMap.keys().begin(), info->receivedChunkMap.keys().end());
    state.isSending = false;

    TransferStateManager::instance().saveTransferState(state);
    return true;
}

bool FileReceiver::receiveChunk(const QString &fileId, int chunkIndex, const QByteArray &data)
{
    QString tempPath;
    QString savePath;
    bool isCompleted = false;

    {
        QMutexLocker locker(&m_mutex);

        // 检查任务是否存在(不存在则不能接收数据)
        if (!m_receivingFiles.contains(fileId)) {
            LOG_ERROR_FMT("File not found:%1,cannot received file",fileId);
            return false;
        }

        ReceivingFileInfo *info = m_receivingFiles[fileId];

        // 检查文件对象是否有效
        if(info->file == nullptr){
            LOG_ERROR_FMT("File object is null for fileId: %1", fileId);
            // 清理资源
            m_receivingFiles.remove(fileId);
            delete info;
            QString errorMsg = "文件对象无效";
            // 锁在这里自动释放
            emit receiveFailed(fileId, errorMsg);
            return false;
        }

        // 检查分片是否已经接收
        if(info->receivedChunkMap.contains(chunkIndex)){
            LOG_WARN_FMT("Chunk already received:%1",chunkIndex);
            return true; // 重复分片，直接忽略
        }

        // 写入文件
        quint64 offset = static_cast<quint64>(chunkIndex) * 64 * 1024;
        if(!info->file->seek(offset)){
            LOG_ERROR_FMT("Failed to write chunk:%1",chunkIndex);
            QString errorMsg = "文件定位失败";
            // 清理资源
            if(info->file){
                info->file->close();
                delete info->file;
            }
            delete info;
            m_receivingFiles.remove(fileId);
            // 锁在这里自动释放
            emit receiveFailed(fileId, errorMsg);
            return false;
        }

        qint64 written = info->file->write(data);
        if(written != (qint64)data.size()){
            // 写入字节数不等于实际字节数时
            LOG_ERROR_FMT("Failed to write chunk:%1",chunkIndex);
            QString errorMsg = "写入文件失败";
            // 清理资源
            if(info->file){
                info->file->close();
                delete info->file;
            }
            delete info;
            m_receivingFiles.remove(fileId);
            // 锁在这里自动释放
            emit receiveFailed(fileId, errorMsg);
            return false;
        }

        // 更新接收信息
        info->receivedChunkMap[chunkIndex] = true;
        info->receivedChunks++;
        info->receivedSize += data.size();

        // 标记分片已完成（每次都要更新内存状态，内部会每10个分片才持久化一次）
        TransferStateManager::instance().markChunkCompleted(fileId, chunkIndex);

        // 计算进度(收到的/总共的)
        int percent = info->totalChunks > 0 ? (info->receivedChunks * 100) / info->totalChunks : 0;

        // 每接收10个分片打印一次日志
        if (chunkIndex % 10 == 0) {
            LOG_INFO(QString("Received chunk %1 / %2 (%3%)").arg(chunkIndex).arg(info->totalChunks).arg(percent));
        }

        emit receiveProgress(fileId,percent,info->receivedSize,info->totalSize);

        // 检查是否接收完成（使用receivedChunkMap.size()确保准确性，避免重复分片影响判断）
        if(info->receivedChunkMap.size() >= info->totalChunks){
            info->file->flush();
            info->file->close();

            // 保存需要的信息，因为后面会删除info
            tempPath = info->tempPath;
            savePath = info->savePath;
            isCompleted = true;

            // 清理接收任务（在锁内完成）
            TransferStateManager::instance().removeTransferState(fileId);
            delete info->file;
            info->file = nullptr; // 防止重复删除
            delete info;
            m_receivingFiles.remove(fileId);
        }
    } // 锁在这里自动释放

    // 在锁外进行文件操作和信号发射，避免死锁
    if(isCompleted){
        // 重命名临时文件为最终文件（不需要锁，因为已经从map中移除了）
        if(!tempPath.isEmpty() && tempPath != savePath && QFile::exists(tempPath)){
            // 如果最终文件已存在，先删除
            if(QFile::exists(savePath)){
                QFile::remove(savePath);
            }

            // 重命名临时文件
            if(QFile::rename(tempPath, savePath)){
                LOG_INFO_FMT("Temp file renamed: %1 -> %2", tempPath, savePath);
            } else {
                LOG_ERROR_FMT("Failed to rename temp file: %1 -> %2", tempPath, savePath);
            }
        }

        LOG_INFO_FMT("File received completed:%1",savePath);

        // 发射信号（在锁外，避免信号处理函数中的死锁）
        emit receiveCompleted(fileId,savePath);
    }

    return true;
}

void FileReceiver::cancelReceiving(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    // 任务列表没有要取消的任务则不处理
    if(!m_receivingFiles.contains(fileId)){
        return;
    }

    ReceivingFileInfo *info = m_receivingFiles[fileId];

    if(info->file){
        info->file->close();
        delete info->file;
    }

    // 不删除未完成的文件，保留用于断点续传
    LOG_INFO_FMT("File receiving canceled:%1",info->savePath);

    delete info;
    m_receivingFiles.remove(fileId);
}

ReceivingFileInfo *FileReceiver::getReceivingInfo(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);
    return m_receivingFiles.value(fileId,nullptr);
}

void FileReceiver::pauseReceiving(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    if(!m_receivingFiles.contains(fileId)){
        return;
    }

    ReceivingFileInfo *info = m_receivingFiles[fileId];
    if(info->file){
        info->file->flush();
    }

    LOG_INFO_FMT("File receiving paused:%1",fileId);
}

void FileReceiver::renameTempFile(const QString &fileId)
{
    QString tempPath;
    QString savePath;

    {
        QMutexLocker locker(&m_mutex);
        
        if(!m_receivingFiles.contains(fileId)){
            return;
        }

        ReceivingFileInfo *info = m_receivingFiles[fileId];

        // 如果没有使用临时文件，直接返回
        if(info->tempPath.isEmpty() || info->tempPath == info->savePath){
            return;
        }

        // 保存路径信息
        tempPath = info->tempPath;
        savePath = info->savePath;
    } // 锁在这里自动释放

    // 在锁外进行文件操作
    // 如果最终文件已存在，先删除
    if(QFile::exists(savePath)){
        QFile::remove(savePath);
    }

    // 重命名临时文件
    if(QFile::rename(tempPath, savePath)){
        LOG_INFO_FMT("Temp file renamed: %1 -> %2", tempPath, savePath);
    } else {
        LOG_ERROR_FMT("Failed to rename temp file: %1 -> %2", tempPath, savePath);
    }
}

QByteArray FileReceiver::getCompletedChunksBitmap(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    if(!m_receivingFiles.contains(fileId)){
        return QByteArray();
    }

    ReceivingFileInfo *info = m_receivingFiles[fileId];
    int bitmapSize = (info->totalChunks + 7) / 8;
    QByteArray bitmap(bitmapSize, 0);

    for(auto it = info->receivedChunkMap.begin(); it != info->receivedChunkMap.end(); ++it){
        int chunkIndex = it.key();
        int byteIndex = chunkIndex / 8;
        int bitIndex = chunkIndex % 8;
        if(byteIndex < bitmap.size()){
            bitmap[byteIndex] = bitmap[byteIndex] | (1 << bitIndex);
        }
    }

    return bitmap;
}

bool FileReceiver::verifyFileMD5(const QString &filePath, const QString &expectedMD5)
{
    if(expectedMD5.isEmpty()){
        return true; // 没有MD5则跳过验证
    }

    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly)){
        LOG_ERROR_FMT("Failed to open file for MD5 verification: %1", filePath);
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    const int bufferSize = 1024 * 1024; // 1MB
    while(!file.atEnd()){
        QByteArray buffer = file.read(bufferSize);
        hash.addData(buffer);
    }
    file.close();

    QString actualMD5 = QString(hash.result().toHex());
    bool verified = (actualMD5 == expectedMD5);

    if(verified){
        LOG_INFO_FMT("File MD5 verified successfully: %1", filePath);
    } else {
        LOG_ERROR_FMT("File MD5 mismatch! Expected: %1, Actual: %2", expectedMD5, actualMD5);
    }

    return verified;
}
