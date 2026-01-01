#include "filereceiver.h"
#include "transferstatemanager.h"
#include "logger.h"
#include "encryptionmanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTimer>
#include <QThread>
#include <QFileInfo>
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
        if(!file->open(QIODevice::WriteOnly)){
            LOG_ERROR_FMT("Failed to create temp file:%1, error: %2", tempPath, file->errorString());
            delete file;
            delete info;
            emit receiveFailed(fileId, QString("无法创建临时文件: %1").arg(file->errorString()));
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
    state.completedChunks = QSet<int>(info->receivedChunkMap.keys().begin(), info->receivedChunkMap.keys().end());
    state.isSending = false;

    TransferStateManager::instance().saveTransferState(state);
    return true;
}

bool FileReceiver::receiveChunk(const QString &fileId, int chunkIndex, const QByteArray &data)
{
    // 在最外层添加异常处理，防止整个函数执行过程中的任何异常导致程序崩溃
    try {
        QString tempPath;
        QString savePath;
        QString expectedMD5;  // 期望的MD5值
        bool isCompleted = false;

        {
            QMutexLocker locker(&m_mutex);

        // 检查任务是否存在(不存在则不能接收数据)
        if (!m_receivingFiles.contains(fileId)) {
            LOG_ERROR_FMT("File not found:%1,cannot received file",fileId);
            return false;
        }

        ReceivingFileInfo *info = m_receivingFiles[fileId];

        // 检查info指针是否有效
        if(info == nullptr){
            LOG_ERROR_FMT("ReceivingFileInfo is null for fileId: %1", fileId);
            m_receivingFiles.remove(fileId);
            emit receiveFailed(fileId, "接收任务信息无效");
            return false;
        }

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

        // 验证分片索引的合理性
        if (chunkIndex < 0 || chunkIndex >= info->totalChunks) {
            LOG_ERROR(QString("Invalid chunk index %1 for file %2 (total chunks: %3)")
                     .arg(chunkIndex).arg(fileId).arg(info->totalChunks));
            emit receiveFailed(fileId, "分片索引无效");
            return false;
        }

        // 检查分片是否已经接收
        if(info->receivedChunkMap.contains(chunkIndex)){
            LOG_WARN_FMT("Chunk already received:%1",chunkIndex);
            return true; // 重复分片，直接忽略
        }

        // 验证数据大小的合理性（最大1MB）
        if (data.size() > 1024 * 1024) {
            LOG_ERROR(QString("Chunk data too large: %1 bytes").arg(data.size()));
            emit receiveFailed(fileId, "分片数据过大");
            return false;
        }

        // 解密文件分片（如果有发送者ID）
        QByteArray decryptedData = data;
        LOG_DEBUG(QString("[FileReceiver] Processing chunk %1, original size: %2").arg(chunkIndex).arg(data.size()));
        
        if (info->senderId > 0 && m_currentUserId > 0) {
            LOG_DEBUG(QString("[FileReceiver] Attempting to decrypt chunk %1 for users %2 and %3")
                     .arg(chunkIndex).arg(m_currentUserId).arg(info->senderId));
            
            // 获取聊天密钥（使用当前用户ID和发送者ID）
            QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, info->senderId);
            
            if (key.isEmpty()) {
                LOG_ERROR_FMT("[FileReceiver] Failed to get encryption key for users %1 and %2", m_currentUserId, info->senderId);
                QString errorMsg = "无法获取解密密钥";
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
            
            LOG_DEBUG(QString("[FileReceiver] Got encryption key, size: %1").arg(key.size()));
            
            // 解密分片数据
            decryptedData = EncryptionManager::instance().xorEncryptDecrypt(data, key);
            
            LOG_DEBUG(QString("[FileReceiver] Decryption completed: input=%1, output=%2")
                     .arg(data.size()).arg(decryptedData.size()));
            
            LOG_DEBUG(QString("[FileReceiver] Decrypted chunk %1: encrypted size=%2, decrypted size=%3")
                     .arg(chunkIndex).arg(data.size()).arg(decryptedData.size()));
        } else {
            LOG_DEBUG(QString("[FileReceiver] No decryption needed for chunk %1 (senderId=%2, currentUserId=%3)")
                     .arg(chunkIndex).arg(info->senderId).arg(m_currentUserId));
        }

        // 写入文件（使用解密后的数据）
        // 使用64位算术避免溢出
        quint64 chunkSize64 = 64 * 1024;
        quint64 offset = static_cast<quint64>(chunkIndex) * chunkSize64;
        
        LOG_DEBUG(QString("[FileReceiver] Preparing to write chunk %1: offset=%2, size=%3")
                 .arg(chunkIndex).arg(offset).arg(decryptedData.size()));
        
        // 验证偏移量是否合理（避免超出文件大小）
        if (offset > static_cast<quint64>(info->totalSize)) {
            LOG_ERROR(QString("File offset %1 exceeds file size %2 for chunk %3")
                     .arg(offset).arg(info->totalSize).arg(chunkIndex));
            QString errorMsg = "文件偏移量超出范围";
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
        
        LOG_DEBUG(QString("[FileReceiver] Seeking to offset %1").arg(offset));
        
        if(!info->file->seek(offset)){
            LOG_ERROR(QString("Failed to seek to offset %1 for chunk %2: %3").arg(offset).arg(chunkIndex).arg(info->file->errorString()));
            QString errorMsg = QString("文件定位失败: %1").arg(info->file->errorString());
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

        LOG_DEBUG(QString("[FileReceiver] Writing %1 bytes to file").arg(decryptedData.size()));

        // 添加异常处理，防止写入过程中的崩溃
        qint64 written = 0;
        try {
            written = info->file->write(decryptedData);
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception during file write for chunk %1: %2").arg(chunkIndex).arg(e.what()));
            QString errorMsg = QString("写入文件时发生异常: %1").arg(e.what());
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
        } catch (...) {
            LOG_ERROR(QString("Unknown exception during file write for chunk %1").arg(chunkIndex));
            QString errorMsg = "写入文件时发生未知异常";
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
        
        LOG_DEBUG(QString("[FileReceiver] Write completed: requested=%1, written=%2")
                 .arg(decryptedData.size()).arg(written));
        
        if(written != (qint64)decryptedData.size()){
            // 写入字节数不等于实际字节数时
            LOG_ERROR(QString("Failed to write chunk %1: expected=%2, written=%3, error=%4")
                     .arg(chunkIndex).arg(decryptedData.size()).arg(written).arg(info->file->errorString()));
            QString errorMsg = QString("写入文件失败: %1").arg(info->file->errorString());
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

        LOG_DEBUG(QString("[FileReceiver] Successfully wrote chunk %1").arg(chunkIndex));

        // 强制刷新文件缓冲区，确保数据写入磁盘
        // 添加异常处理，防止刷新过程中的崩溃
        try {
            if (!info->file->flush()) {
                LOG_ERROR(QString("Failed to flush file buffer for chunk %1: %2").arg(chunkIndex).arg(info->file->errorString()));
                QString errorMsg = QString("文件缓冲区刷新失败: %1").arg(info->file->errorString());
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
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception during file flush for chunk %1: %2").arg(chunkIndex).arg(e.what()));
            // 刷新失败不一定是致命错误，继续执行
            LOG_WARN("File flush failed but continuing");
        } catch (...) {
            LOG_ERROR(QString("Unknown exception during file flush for chunk %1").arg(chunkIndex));
            // 刷新失败不一定是致命错误，继续执行
            LOG_WARN("File flush failed but continuing");
        }

        // 更新接收信息（使用解密后的数据大小）
        info->receivedChunkMap[chunkIndex] = true;
        info->receivedChunks++;
        info->receivedSize += decryptedData.size();

        // 标记分片已完成（每次都要更新内存状态，内部会每10个分片才持久化一次）
        TransferStateManager::instance().markChunkCompleted(fileId, chunkIndex);

        // 计算进度(收到的/总共的)
        int percent = info->totalChunks > 0 ? (info->receivedChunks * 100) / info->totalChunks : 0;

        // 每接收10个分片打印一次日志
        if (chunkIndex % 10 == 0) {
            LOG_INFO(QString("Received chunk %1 / %2 (%3%)").arg(chunkIndex).arg(info->totalChunks).arg(percent));
        }

        // 发射进度信号，添加异常处理防止崩溃
        try {
            emit receiveProgress(fileId,percent,info->receivedSize,info->totalSize);
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in receiveProgress signal handler: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in receiveProgress signal handler");
        }

        // 检查是否接收完成（使用receivedChunkMap.size()确保准确性，避免重复分片影响判断）
        if(info->receivedChunkMap.size() >= info->totalChunks){
            // 添加异常处理，防止关闭文件时的崩溃
            try {
                // 确保所有数据都写入磁盘
                if (!info->file->flush()) {
                    LOG_WARN(QString("Failed to flush file before close: %1").arg(info->file->errorString()));
                }
                
                // 关闭文件
                info->file->close();
            } catch (const std::exception& e) {
                LOG_ERROR(QString("Exception during file close: %1").arg(e.what()));
                // 即使关闭失败，也继续处理，因为文件可能已经写入
            } catch (...) {
                LOG_ERROR("Unknown exception during file close");
                // 即使关闭失败，也继续处理
            }

            // 保存需要的信息，因为后面会删除info
            tempPath = info->tempPath;
            savePath = info->savePath;
            expectedMD5 = info->expectedMD5;  // 保存期望的MD5值
            isCompleted = true;

            // 清理接收任务（在锁内完成）
            try {
                TransferStateManager::instance().removeTransferState(fileId);
            } catch (const std::exception& e) {
                LOG_ERROR(QString("Exception during transfer state removal: %1").arg(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown exception during transfer state removal");
            }
            
            delete info->file;
            info->file = nullptr; // 防止重复删除
            delete info;
            m_receivingFiles.remove(fileId);
        }
    } // 锁在这里自动释放

    // 在锁外进行文件操作和信号发射，避免死锁
    if(isCompleted){
        // 重命名临时文件为最终文件（不需要锁，因为已经从map中移除了）
        // 添加异常处理，防止文件操作导致崩溃
        try {
            if(!tempPath.isEmpty() && tempPath != savePath && QFile::exists(tempPath)){
            // 检测是否为可执行文件
            QFileInfo fileInfo(savePath);
            bool isExecutable = (fileInfo.suffix().toLower() == "exe" || 
                                fileInfo.suffix().toLower() == "dll" ||
                                fileInfo.suffix().toLower() == "bat" ||
                                fileInfo.suffix().toLower() == "cmd" ||
                                fileInfo.suffix().toLower() == "com" ||
                                fileInfo.suffix().toLower() == "scr");
            
            // 对于可执行文件，需要特别小心处理
            // Windows防病毒软件可能会在文件重命名为.exe时进行扫描，导致程序被终止
            // 因此需要添加延迟和异常处理
            
            // 等待一小段时间，确保文件句柄完全释放
            // 对于可执行文件，需要更长的延迟
            if(isExecutable){
                QThread::msleep(500); // 可执行文件需要更长的延迟
                LOG_DEBUG("Executable file detected, using longer delay");
            } else {
                QThread::msleep(100); // 普通文件较短的延迟即可
            }
            
            // 如果最终文件已存在，先删除
            if(QFile::exists(savePath)){
                if(!QFile::remove(savePath)){
                    LOG_WARN_FMT("Failed to remove existing file: %1, will try to overwrite", savePath);
                }
            }

            // 尝试重命名临时文件
            bool renameSuccess = false;
            try {
                // 首先尝试直接重命名（最快的方式）
                if(QFile::rename(tempPath, savePath)){
                    renameSuccess = true;
                    LOG_INFO_FMT("Temp file renamed: %1 -> %2", tempPath, savePath);
                } else {
                    // 如果直接重命名失败（可能是防病毒软件干扰），尝试复制+删除的方式
                    LOG_WARN_FMT("Direct rename failed, trying copy+delete method: %1 -> %2", tempPath, savePath);
                    
                    // 再次等待，给系统一些时间
                    // 对于可执行文件，需要更长的延迟
                    if(isExecutable){
                        QThread::msleep(500);
                    } else {
                        QThread::msleep(200);
                    }
                    
                    // 尝试复制文件
                    QFile sourceFile(tempPath);
                    QFile destFile(savePath);
                    
                    bool copySuccess = false;
                    try {
                        if(sourceFile.open(QIODevice::ReadOnly) && destFile.open(QIODevice::WriteOnly)){
                            // 分块复制，避免大文件一次性加载到内存
                            const qint64 bufferSize = 64 * 1024; // 64KB
                            qint64 totalBytes = sourceFile.size();
                            qint64 bytesCopied = 0;
                            
                            while(bytesCopied < totalBytes){
                                QByteArray buffer = sourceFile.read(bufferSize);
                                
                                // 检查读取是否成功
                                if(buffer.isEmpty() && !sourceFile.atEnd()){
                                    LOG_ERROR("Failed to read from source file during copy");
                                    break;
                                }
                                
                                // 如果已经到达文件末尾，但还有数据，继续处理
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
                                
                                // 确保数据写入磁盘
                                if(!destFile.flush()){
                                    LOG_WARN("Failed to flush during file copy, continuing...");
                                }
                            }
                            
                            // 确保目标文件的所有数据都写入磁盘
                            destFile.flush();
                            destFile.close();
                            sourceFile.close();
                            
                            if(bytesCopied == totalBytes){
                                // 对于可执行文件，在删除临时文件前需要等待，让防病毒软件完成扫描
                                if(isExecutable){
                                    QThread::msleep(300);
                                    LOG_DEBUG("Waiting for antivirus scan to complete before removing temp file");
                                }
                                
                                // 复制成功，删除临时文件
                                if(QFile::remove(tempPath)){
                                    renameSuccess = true;
                                    copySuccess = true;
                                    LOG_INFO_FMT("File copied and temp file removed: %1 -> %2", tempPath, savePath);
                                } else {
                                    LOG_WARN_FMT("File copied but failed to remove temp file: %1", tempPath);
                                    // 即使删除临时文件失败，复制也算成功
                                    renameSuccess = true;
                                    copySuccess = true;
                                }
                            } else {
                                LOG_ERROR_FMT("File copy incomplete: copied %1/%2 bytes", bytesCopied, totalBytes);
                                // 清理不完整的文件
                                try {
                                    destFile.remove();
                                } catch (...) {
                                    LOG_ERROR("Failed to remove incomplete destination file");
                                }
                            }
                        } else {
                            LOG_ERROR_FMT("Failed to open files for copy: source=%1, dest=%2", 
                                         sourceFile.errorString(), destFile.errorString());
                            // 确保已打开的文件被关闭
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR_FMT("Exception during file copy: %1", e.what());
                        // 确保文件被关闭
                        try {
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                            // 清理可能不完整的文件
                            if(QFile::exists(savePath) && !copySuccess){
                                destFile.remove();
                            }
                        } catch (...) {
                            // 忽略清理时的异常
                        }
                    } catch (...) {
                        LOG_ERROR("Unknown exception during file copy");
                        // 确保文件被关闭
                        try {
                            if(sourceFile.isOpen()) sourceFile.close();
                            if(destFile.isOpen()) destFile.close();
                            // 清理可能不完整的文件
                            if(QFile::exists(savePath) && !copySuccess){
                                destFile.remove();
                            }
                        } catch (...) {
                            // 忽略清理时的异常
                        }
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR_FMT("Exception during file rename: %1", e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception during file rename");
            }
            
            if(!renameSuccess){
                LOG_ERROR_FMT("Failed to rename temp file: %1 -> %2", tempPath, savePath);
                // 清理临时文件（如果重命名失败）
                try {
                    if(QFile::exists(tempPath)){
                        // 不删除临时文件，保留用于后续手动恢复
                        LOG_WARN_FMT("Temp file preserved for manual recovery: %1", tempPath);
                    }
                } catch (...) {
                    LOG_ERROR("Failed to handle temp file after rename failure");
                }
                emit receiveFailed(fileId, "重命名临时文件失败，可能是防病毒软件干扰");
                return false;
            }
            
            // 对于可执行文件，重命名成功后需要等待，让防病毒软件完成扫描
            // 这样可以避免在后续操作（如MD5验证）时触发防病毒软件扫描导致程序终止
            if(isExecutable){
                QThread::msleep(500);
                LOG_DEBUG("Waiting for antivirus scan to complete after file rename");
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

        // 验证文件MD5（如果提供了期望的MD5值）
        if (!expectedMD5.isEmpty()) {
            // 首先检查文件是否存在
            if (!QFile::exists(savePath)) {
                LOG_ERROR_FMT("File does not exist for MD5 verification: %1", savePath);
                emit receiveFailed(fileId, "文件不存在，无法验证完整性");
                return false;
            }
            
            // 检测是否为可执行文件
            QFileInfo fileInfo(savePath);
            bool isExecutable = (fileInfo.suffix().toLower() == "exe" || 
                                fileInfo.suffix().toLower() == "dll" ||
                                fileInfo.suffix().toLower() == "bat" ||
                                fileInfo.suffix().toLower() == "cmd" ||
                                fileInfo.suffix().toLower() == "com" ||
                                fileInfo.suffix().toLower() == "scr");
            
            // 对于可执行文件，在MD5验证前需要等待，让防病毒软件完成扫描
            if(isExecutable){
                QThread::msleep(500);  // 增加等待时间，确保防病毒软件完成扫描
                LOG_DEBUG("Waiting before MD5 verification for executable file");
            } else {
                QThread::msleep(100);  // 普通文件也需要短暂等待，确保文件系统同步
            }
            
            try {
                bool verified = verifyFileMD5(savePath, expectedMD5);
                if (!verified) {
                    LOG_ERROR_FMT("File MD5 verification failed for: %1", savePath);
                    emit receiveFailed(fileId, "文件完整性验证失败");
                    return false;
                }
                LOG_INFO_FMT("File MD5 verified successfully: %1", savePath);
            } catch (const std::exception& e) {
                LOG_ERROR_FMT("Exception during MD5 verification: %1", e.what());
                // MD5验证失败不应该导致传输失败，只记录警告
                LOG_WARN("MD5 verification failed due to exception, but file transfer completed");
            } catch (...) {
                LOG_ERROR("Unknown exception during MD5 verification");
                // MD5验证失败不应该导致传输失败，只记录警告
                LOG_WARN("MD5 verification failed due to unknown exception, but file transfer completed");
            }
        }

        LOG_INFO_FMT("File received completed:%1",savePath);

        // 发射信号（在锁外，避免信号处理函数中的死锁）
        // 直接发射信号，Qt的信号槽机制会自动处理线程安全
        // 添加异常处理，防止信号处理函数中的问题导致崩溃
        try {
            emit receiveCompleted(fileId,savePath);
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in receiveCompleted signal handler: %1", e.what());
            // 信号处理失败不应该影响文件接收的成功
        } catch (...) {
            LOG_ERROR("Unknown exception in receiveCompleted signal handler");
            // 信号处理失败不应该影响文件接收的成功
        }
    }

    return true;
    } catch (const std::exception& e) {
        // 捕获所有标准异常，防止程序崩溃
        LOG_ERROR(QString("[FileReceiver] Exception in receiveChunk: %1 (fileId=%2, chunkIndex=%3)")
                 .arg(e.what()).arg(fileId).arg(chunkIndex));
        // 尝试清理资源
        try {
            QMutexLocker locker(&m_mutex);
            if (m_receivingFiles.contains(fileId)) {
                ReceivingFileInfo *info = m_receivingFiles[fileId];
                if (info && info->file) {
                    info->file->close();
                    delete info->file;
                }
                delete info;
                m_receivingFiles.remove(fileId);
            }
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to cleanup resources after exception");
        }
        // 发射失败信号
        try {
            emit receiveFailed(fileId, QString("接收分片时发生异常: %1").arg(e.what()));
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to emit receiveFailed signal");
        }
        return false;
    } catch (...) {
        // 捕获所有其他异常，防止程序崩溃
        LOG_ERROR(QString("[FileReceiver] Unknown exception in receiveChunk (fileId=%1, chunkIndex=%2)")
                 .arg(fileId).arg(chunkIndex));
        // 尝试清理资源
        try {
            QMutexLocker locker(&m_mutex);
            if (m_receivingFiles.contains(fileId)) {
                ReceivingFileInfo *info = m_receivingFiles[fileId];
                if (info && info->file) {
                    info->file->close();
                    delete info->file;
                }
                delete info;
                m_receivingFiles.remove(fileId);
            }
        } catch (...) {
            LOG_ERROR("[FileReceiver] Failed to cleanup resources after unknown exception");
        }
        // 发射失败信号
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
