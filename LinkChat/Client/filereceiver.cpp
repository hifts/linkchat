#include "filereceiver.h"
#include "transferstatemanager.h"
#include "logger.h"
#include "encryptionmanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <qdebug.h>

namespace {
constexpr qint64 kFileChunkSize = 64 * 1024;

void destroyReceivingInfo(ReceivingFileInfo *info)
{
    if (!info) {
        return;
    }

    if (info->file) {
        if (info->file->isOpen()) {
            info->file->close();
        }
        delete info->file;
        info->file = nullptr;
    }

    delete info;
}

bool isExecutableLikeFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == "exe" ||
           suffix == "dll" ||
           suffix == "bat" ||
           suffix == "cmd" ||
           suffix == "com" ||
           suffix == "scr";
}
}

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

bool FileReceiver::startReceiving(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId, const QString &expectedMD5)
{
    LOG_DEBUG(QString("[FileReceiver] startReceiving called: fileId=%1, fileName=%2, fileSize=%3, senderId=%4")
             .arg(fileId).arg(fileName).arg(fileSize).arg(senderId));
    
    // 先进行基本的参数验证，避免在锁内出现问题
    if (fileId.isEmpty() || fileName.isEmpty() || fileSize <= 0) {
        LOG_ERROR(QString("[FileReceiver] Invalid parameters: fileId=%1, fileName=%2, fileSize=%3")
                 .arg(fileId).arg(fileName).arg(fileSize));
        emit receiveFailed(fileId, "参数无效");
        return false;
    }
    
    try {
        QMutexLocker locker(&m_mutex);
        
        
        LOG_DEBUG(QString("[FileReceiver] Mutex locked successfully"));

        // 检查文件大小是否有效
        if(fileSize <= 0){
            LOG_ERROR_FMT("Invalid file size: %1", fileSize);
            emit receiveFailed(fileId, "文件大小无效");
            return false;
        }
        
        LOG_DEBUG(QString("[FileReceiver] File size validation passed"));
        
        return startReceivingInternal(fileId, fileName, fileSize, senderId, expectedMD5);
        
    } catch (const std::exception& e) {
        LOG_ERROR(QString("[FileReceiver] Exception in startReceiving: %1").arg(e.what()));
        emit receiveFailed(fileId, "内部错误");
        return false;
    } catch (...) {
        LOG_ERROR("[FileReceiver] Unknown exception in startReceiving");
        emit receiveFailed(fileId, "未知错误");
        return false;
    }
}

bool FileReceiver::startReceivingInternal(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId, const QString &expectedMD5)
{

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
    
    LOG_DEBUG(QString("[FileReceiver] Cleanup completed, loading transfer state"));

    // 检查是否存在断点续传状态
    TransferState saveState = TransferStateManager::instance().loadTransferState(fileId);
    
    LOG_DEBUG(QString("[FileReceiver] Transfer state loaded, creating info object"));

    QString savePath;
    QString tempPath;
    QFile *file = nullptr;
    ReceivingFileInfo *info = new ReceivingFileInfo;
    
    if (info == nullptr) {
        LOG_ERROR("[FileReceiver] Failed to allocate ReceivingFileInfo");
        emit receiveFailed(fileId, "内存分配失败");
        return false;
    }
    
    LOG_DEBUG(QString("[FileReceiver] Info object created successfully"));

    QString downloadPath = QCoreApplication::applicationDirPath() + "/ReceivedFiles";
    LOG_DEBUG(QString("[FileReceiver] Download path: %1").arg(downloadPath));
    
    if (!QDir().mkpath(downloadPath)) {
        LOG_ERROR_FMT("Failed to create download directory: %1", downloadPath);
        delete info;
        emit receiveFailed(fileId, "无法创建下载目录");
        return false;
    }

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
            info->receivedSize = qMin<qint64>(
                fileSize,
                static_cast<qint64>(saveState.completedChunks.size()) * kFileChunkSize);

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

        // 清理文件名中的非法字符
        QString cleanFileName = fileName;
        cleanFileName.replace(QRegExp("[<>:\"/\\|?*]"), "_");
        savePath = downloadPath + "/" + cleanFileName;

        // 如果最终文件存在，添加序号
        int count = 1;
        while (QFile::exists(savePath)) {
            QFileInfo fi(cleanFileName);
            QString baseName = fi.completeBaseName();
            QString ext = fi.suffix();
            savePath = downloadPath + "/" + baseName + QString("(%1).").arg(count) + ext;
            count++;
        }

        // 使用.tmp扩展名创建临时文件
        tempPath = savePath + ".tmp";

        // 确保目录存在
        QDir dir = QFileInfo(tempPath).absoluteDir();
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        file = new QFile(tempPath);
        
        // 尝试以二进制模式打开文件
        if(!file->open(QIODevice::ReadWrite)){
            const QString fileError = file->errorString();
            LOG_ERROR_FMT("Failed to create temp file:%1, error: %2", tempPath, fileError);
            delete file;
            delete info;
            emit receiveFailed(fileId, QString("无法创建临时文件: %1").arg(fileError));
            return false;
        }

        // 验证文件是否真的可写
        if (!file->isWritable()) {
            LOG_ERROR_FMT("Temp file is not writable: %1", tempPath);
            file->close();
            delete file;
            delete info;
            emit receiveFailed(fileId, "临时文件不可写");
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
    info->senderId = senderId;          // 存储发送者ID用于解密
    info->expectedMD5 = expectedMD5;    // 存储期望的MD5值

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
    const QList<int> completedChunkKeys = info->receivedChunkMap.keys();
    state.completedChunks = QSet<int>(completedChunkKeys.cbegin(), completedChunkKeys.cend());
    state.isSending = false;

    TransferStateManager::instance().saveTransferState(state);
    return true;
}

bool FileReceiver::receiveChunk(const QString &fileId, int chunkIndex, const QByteArray &data)
{
    try {
        ReceiveChunkResult result;

        {
            QMutexLocker locker(&m_mutex);
            result = receiveChunkLocked(fileId, chunkIndex, data);
        }

        if (!result.errorMessage.isEmpty()) {
            emit receiveFailed(fileId, result.errorMessage);
            return false;
        }

        try {
            emit receiveProgress(fileId, result.progressPercent, result.receivedSize, result.totalSize);
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in receiveProgress signal handler: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in receiveProgress signal handler");
        }

        if (chunkIndex % 10 == 0) {
            LOG_INFO(QString("Received chunk %1 for file %2 (%3%)")
                     .arg(chunkIndex).arg(fileId).arg(result.progressPercent));
        }

        if(result.completed){
        try {
            if(!result.tempPath.isEmpty() && result.tempPath != result.savePath && QFile::exists(result.tempPath)){
            bool isExecutable = isExecutableLikeFile(result.savePath);
            if(isExecutable){
                QThread::msleep(500);
            } else {
                QThread::msleep(100);
            }
            
            if(QFile::exists(result.savePath)){
                if(!QFile::remove(result.savePath)){
                    LOG_WARN_FMT("Failed to remove existing file: %1, will try to overwrite", result.savePath);
                }
            }

            // 尝试重命名临时文件
            bool renameSuccess = false;
            try {
                // 首先尝试直接重命名（最快的方式）
                if(QFile::rename(result.tempPath, result.savePath)){
                    renameSuccess = true;
                    LOG_INFO_FMT("Temp file renamed: %1 -> %2", result.tempPath, result.savePath);
                } else {
                    LOG_WARN_FMT("Direct rename failed, trying copy+delete method: %1 -> %2", result.tempPath, result.savePath);
                    
                    if(isExecutable){
                        QThread::msleep(500);
                    } else {
                        QThread::msleep(200);
                    }
                    
                    // 尝试复制文件
                    QFile sourceFile(result.tempPath);
                    QFile destFile(result.savePath);
                    
                    bool copySuccess = false;
                    try {
                        if(sourceFile.open(QIODevice::ReadOnly) && destFile.open(QIODevice::WriteOnly)){
                            const qint64 bufferSize = kFileChunkSize;
                            qint64 totalBytes = sourceFile.size();
                            qint64 bytesCopied = 0;
                            
                            while(bytesCopied < totalBytes){
                                QByteArray buffer = sourceFile.read(bufferSize);
                                if(buffer.isEmpty() && !sourceFile.atEnd()){
                                    LOG_ERROR("Failed to read from source file during copy");
                                    break;
                                }
                                if(buffer.isEmpty() && sourceFile.atEnd()){
                                    break;
                                }
                                
                                qint64 written = destFile.write(buffer);
                                if(written != buffer.size()){
                                    LOG_ERROR(QString("Failed to write to destination file during copy: expected=%1, written=%2")
                                             .arg(buffer.size()).arg(written));
                                    break;
                                }
                                
                                bytesCopied += written;
                                if(!destFile.flush()){
                                    LOG_WARN("Failed to flush during file copy, continuing...");
                                }
                            }
                            
                            destFile.flush();
                            destFile.close();
                            sourceFile.close();
                            
                            if(bytesCopied == totalBytes){
                                if(isExecutable){
                                    QThread::msleep(300);
                                }
                                if(QFile::remove(result.tempPath)){
                                    renameSuccess = true;
                                    copySuccess = true;
                                    LOG_INFO_FMT("File copied and temp file removed: %1 -> %2", result.tempPath, result.savePath);
                                } else {
                                    LOG_WARN_FMT("File copied but failed to remove temp file: %1", result.tempPath);
                                    renameSuccess = true;
                                    copySuccess = true;
                                }
                            } else {
                                LOG_ERROR_FMT("File copy incomplete: copied %1/%2 bytes", bytesCopied, totalBytes);
                                try {
                                    destFile.remove();
                                } catch (...) {
                                    LOG_ERROR("Failed to remove incomplete destination file");
                                }
                            }
                        } else {
                            LOG_ERROR_FMT("Failed to open files for copy: source=%1, dest=%2", 
                                         sourceFile.errorString(), destFile.errorString());
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR_FMT("Exception during file copy: %1", e.what());
                        try {
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                            if(QFile::exists(result.savePath) && !copySuccess){
                                destFile.remove();
                            }
                        } catch (...) {
                        }
                    } catch (...) {
                        LOG_ERROR("Unknown exception during file copy");
                        try {
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                            if(QFile::exists(result.savePath) && !copySuccess){
                                destFile.remove();
                            }
                        } catch (...) {
                        }
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR_FMT("Exception during file rename: %1", e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception during file rename");
            }
            
            if(!renameSuccess){
                LOG_ERROR_FMT("Failed to rename temp file: %1 -> %2", result.tempPath, result.savePath);
                try {
                    if(QFile::exists(result.tempPath)){
                        LOG_WARN_FMT("Temp file preserved for manual recovery: %1", result.tempPath);
                    }
                } catch (...) {
                    LOG_ERROR("Failed to handle temp file after rename failure");
                }
                emit receiveFailed(fileId, "重命名临时文件失败，可能是防病毒软件干扰");
                return false;
            }
            
            if(isExecutable){
                QThread::msleep(500);
            }
            }
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception during file rename operation: %1", e.what());
            emit receiveFailed(fileId, QString("文件重命名时发生异常: %1").arg(e.what()));
            return false;
        } catch (...) {
            LOG_ERROR("Unknown exception during file rename operation");
            emit receiveFailed(fileId, "文件重命名时发生未知异常");
            return false;
        }

        if (!result.expectedMD5.isEmpty()) {
            if (!QFile::exists(result.savePath)) {
                LOG_ERROR_FMT("File does not exist for MD5 verification: %1", result.savePath);
                emit receiveFailed(fileId, "文件不存在，无法验证完整性");
                return false;
            }
            
            bool isExecutable = isExecutableLikeFile(result.savePath);
            if(isExecutable){
                QThread::msleep(500);
            } else {
                QThread::msleep(100);
            }
            
            try {
                bool verified = verifyFileMD5(result.savePath, result.expectedMD5);
                if (!verified) {
                    LOG_ERROR_FMT("File MD5 verification failed for: %1", result.savePath);
                    emit receiveFailed(fileId, "文件完整性验证失败");
                    return false;
                }
                LOG_INFO_FMT("File MD5 verified successfully: %1", result.savePath);
            } catch (const std::exception& e) {
                LOG_ERROR_FMT("Exception during MD5 verification: %1", e.what());
                LOG_WARN("MD5 verification failed due to exception, but file transfer completed");
            } catch (...) {
                LOG_ERROR("Unknown exception during MD5 verification");
                LOG_WARN("MD5 verification failed due to unknown exception, but file transfer completed");
            }
        }

        LOG_INFO_FMT("File received completed:%1",result.savePath);

        try {
            emit receiveCompleted(fileId,result.savePath);
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in receiveCompleted signal handler: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in receiveCompleted signal handler");
        }
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(QString("[FileReceiver] Exception in receiveChunk: %1 (fileId=%2, chunkIndex=%3)")
                 .arg(e.what()).arg(fileId).arg(chunkIndex));
        try {
            QMutexLocker locker(&m_mutex);
            if (m_receivingFiles.contains(fileId)) {
                ReceivingFileInfo *info = m_receivingFiles[fileId];
                m_receivingFiles.remove(fileId);
                destroyReceivingInfo(info);
            }
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to cleanup resources after exception");
        }
        try {
            emit receiveFailed(fileId, QString("接收分片时发生异常: %1").arg(e.what()));
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to emit receiveFailed signal");
        }
        return false;
    } catch (...) {
        LOG_ERROR(QString("[FileReceiver] Unknown exception in receiveChunk (fileId=%1, chunkIndex=%2)")
                 .arg(fileId).arg(chunkIndex));
        try {
            QMutexLocker locker(&m_mutex);
            if (m_receivingFiles.contains(fileId)) {
                ReceivingFileInfo *info = m_receivingFiles[fileId];
                m_receivingFiles.remove(fileId);
                destroyReceivingInfo(info);
            }
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to cleanup resources after unknown exception");
        }
        try {
            emit receiveFailed(fileId, "接收分片时发生未知异常");
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to emit receiveFailed signal");
        }
        return false;
    }
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

ReceiveChunkResult FileReceiver::receiveChunkLocked(const QString &fileId, int chunkIndex, const QByteArray &data)
{
    ReceiveChunkResult result;

    if (!m_receivingFiles.contains(fileId)) {
        LOG_ERROR_FMT("File not found:%1,cannot received file",fileId);
        result.errorMessage = "接收任务不存在";
        return result;
    }

    ReceivingFileInfo *info = m_receivingFiles[fileId];
    result.errorMessage = validateChunk(fileId, info, chunkIndex, data);
    if (!result.errorMessage.isEmpty()) {
        if (info && result.errorMessage != "分片已接收") {
            cleanupFailedReceive(fileId, info);
        }
        if (result.errorMessage == "分片已接收") {
            result.errorMessage.clear();
        }
        return result;
    }

    QString errorMessage;
    const QByteArray decryptedData = decryptChunk(info, data, &errorMessage);
    if (errorMessage.isEmpty()) {
        writeChunkToFile(info, chunkIndex, decryptedData, &errorMessage);
    }

    if (!errorMessage.isEmpty()) {
        cleanupFailedReceive(fileId, info);
        result.errorMessage = errorMessage;
        return result;
    }

    return markChunkStored(fileId, info, chunkIndex, decryptedData.size());
}

QString FileReceiver::validateChunk(const QString &fileId, const ReceivingFileInfo *info, int chunkIndex, const QByteArray &data) const
{
    if (info == nullptr) {
        return "接收任务信息无效";
    }
    if (info->file == nullptr) {
        return "文件对象无效";
    }
    if (info->totalChunks <= 0 || info->totalSize <= 0) {
        return "接收状态无效";
    }
    if (chunkIndex < 0 || chunkIndex >= info->totalChunks) {
        return "分片索引无效";
    }
    if (info->receivedChunkMap.contains(chunkIndex)) {
        return "分片已接收";
    }
    if (data.isEmpty() || data.size() > 1024 * 1024) {
        return "分片数据无效";
    }

    Q_UNUSED(fileId)
    return QString();
}

QByteArray FileReceiver::decryptChunk(const ReceivingFileInfo *info, const QByteArray &data, QString *errorMessage) const
{
    QByteArray decryptedData = data;
    if (info->senderId > 0 && m_currentUserId > 0) {
        const QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, info->senderId);
        if (key.isEmpty()) {
            if (errorMessage) *errorMessage = "无法获取解密密钥";
            return QByteArray();
        }
        decryptedData = EncryptionManager::instance().xorEncryptDecrypt(data, key);
    }
    return decryptedData;
}

bool FileReceiver::writeChunkToFile(ReceivingFileInfo *info, int chunkIndex, const QByteArray &data, QString *errorMessage)
{
    const qint64 offset = static_cast<qint64>(chunkIndex) * kFileChunkSize;
    const qint64 remaining = info->totalSize - offset;
    const qint64 expectedChunkSize = qMin<qint64>(kFileChunkSize, remaining);

    if (remaining <= 0 || expectedChunkSize <= 0) {
        if (errorMessage) *errorMessage = "文件偏移量超出范围";
        return false;
    }
    if (data.size() != expectedChunkSize) {
        LOG_ERROR(QString("Chunk size mismatch for file %1, chunk %2: expected=%3, actual=%4")
                  .arg(info->fileId).arg(chunkIndex).arg(expectedChunkSize).arg(data.size()));
        if (errorMessage) *errorMessage = "分片大小异常";
        return false;
    }
    if (!info->file->seek(offset)) {
        if (errorMessage) *errorMessage = QString("文件定位失败: %1").arg(info->file->errorString());
        return false;
    }

    const qint64 written = info->file->write(data);
    if (written != data.size()) {
        if (errorMessage) *errorMessage = QString("写入文件失败: %1").arg(info->file->errorString());
        return false;
    }
    if (!info->file->flush()) {
        if (errorMessage) *errorMessage = QString("文件缓冲区刷新失败: %1").arg(info->file->errorString());
        return false;
    }
    return true;
}

ReceiveChunkResult FileReceiver::markChunkStored(const QString &fileId, ReceivingFileInfo *info, int chunkIndex, qint64 chunkSize)
{
    ReceiveChunkResult result;
    info->receivedChunkMap[chunkIndex] = true;
    info->receivedChunks = info->receivedChunkMap.size();
    info->receivedSize += chunkSize;
    result.progressPercent = (info->receivedChunks * 100) / info->totalChunks;
    result.receivedSize = info->receivedSize;
    result.totalSize = info->totalSize;

    TransferStateManager::instance().markChunkCompleted(fileId, chunkIndex);

    if (info->receivedChunkMap.size() >= info->totalChunks) {
        info->file->flush();
        info->file->close();
        result.tempPath = info->tempPath;
        result.savePath = info->savePath;
        result.expectedMD5 = info->expectedMD5;
        result.completed = true;
        TransferStateManager::instance().removeTransferState(fileId);
        TransferStateManager::instance().flush();
        m_receivingFiles.remove(fileId);
        info->file = nullptr;
        delete info;
    }

    return result;
}

void FileReceiver::cleanupFailedReceive(const QString &fileId, ReceivingFileInfo *info)
{
    m_receivingFiles.remove(fileId);
    destroyReceivingInfo(info);
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
    TransferStateManager::instance().flush();

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

    // 检查文件路径是否有效
    if(filePath.isEmpty()){
        LOG_ERROR("[FileReceiver] Empty file path for MD5 verification");
        return false;
    }

    // 检查文件是否存在
    if(!QFile::exists(filePath)){
        LOG_ERROR_FMT("File does not exist for MD5 verification: %1", filePath);
        return false;
    }

    try {
        QFile file(filePath);
        if(!file.open(QIODevice::ReadOnly)){
            LOG_ERROR_FMT("Failed to open file for MD5 verification: %1, error: %2", filePath, file.errorString());
            return false;
        }

        QCryptographicHash hash(QCryptographicHash::Md5);
        const int bufferSize = 1024 * 1024; // 1MB
        
        while(!file.atEnd()){
            QByteArray buffer = file.read(bufferSize);
            
            // 检查读取是否成功
            if(buffer.isEmpty() && !file.atEnd()){
                LOG_ERROR("Failed to read file data during MD5 verification");
                file.close();
                return false;
            }
            
            // 如果已经到达文件末尾，但还有数据，继续处理
            if(buffer.isEmpty() && file.atEnd()){
                break;
            }
            
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
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Exception in verifyFileMD5: %1", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown exception in verifyFileMD5");
        return false;
    }
}
