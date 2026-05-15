#include "filetransfermanager.h"
#include "transferstatemanager.h"
#include "logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <qdebug.h>

FileTransferManager &FileTransferManager::instance()
{
    static FileTransferManager instance;
    return instance;
}

FileTransferManager::FileTransferManager(QObject *parent)
    : QObject{parent}
    , m_currentUserId(0)
{
    m_dispatchTimer = new QTimer(this);
    m_dispatchTimer->setInterval(300);
    connect(m_dispatchTimer, &QTimer::timeout, this, &FileTransferManager::dispatchPendingChunks);
}

FileTransferManager::~FileTransferManager()
{
    for(auto it = m_transfers.begin();it != m_transfers.end();++it){
        if(it.value()->thread){
            it.value()->thread->stopTransfer();
            it.value()->thread->wait();
            delete it.value()->thread;
        }
        delete it.value();
    }
    m_transfers.clear();
}

QString FileTransferManager::generateFileId(const QString &filePath)
{
    QString data = filePath + QString::number(QDateTime::currentMSecsSinceEpoch());
    QByteArray hash = QCryptographicHash::hash(data.toUtf8(),QCryptographicHash::Md5);
    return QString(hash.toHex());
}

void FileTransferManager::setCurrentUserId(int userId)
{
    m_currentUserId = userId;
    LOG_INFO_FMT("FileTransferManager: Current user ID set to %1", userId);
}

QString FileTransferManager::startSendFile(const QString &fileId,const QString &filePath, int friendId)
{
    QFileInfo fileInfo(filePath);

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        LOG_WARN_FMT("File not found:%1",filePath);
        return QString();
    }

    QString fid = fileId;

    FileTransferInfo *info = new FileTransferInfo;
    info->fileId = fileId;
    info->fileName = fileInfo.fileName();
    info->filePath = filePath;
    info->fileSize = fileInfo.size();
    info->friendId = friendId;
    info->isReceiving = false;
    info->progress = 0;
    info->totalChunks = (info->fileSize + 64 * 1024 - 1) / (64 * 1024);
    info->allChunksSent = false;
    info->thread = new FileTransferThread(filePath,fileId,friendId,m_currentUserId,this);

    TransferState savedState = TransferStateManager::instance().loadTransferState(fileId);
    if(!savedState.fileId.isEmpty() && savedState.isSending && savedState.fileSize == info->fileSize){
        info->thread->setCompletedChunks(savedState.completedChunks);
        info->progress = savedState.totalChunks > 0 ?
            (savedState.completedChunks.size() * 100) / savedState.totalChunks : 0;
        LOG_INFO(QString("Resuming file transfer: %1, completed chunks: %2/%3")
                     .arg(info->fileName).arg(savedState.completedChunks.size()).arg(savedState.totalChunks));
    } else {
        TransferState state;
        state.fileId = fileId;
        state.fileName = info->fileName;
        state.filePath = filePath;
        state.fileSize = info->fileSize;
        state.friendId = friendId;
        state.isSending = true;
        state.totalChunks = info->totalChunks;
        state.completedChunks = QSet<int>();
        state.timestamp = QDateTime::currentSecsSinceEpoch();

        if(info->fileSize < 50 * 1024 * 1024) {
            state.fileMD5 = info->thread->calculateFileMD5();
        }

        TransferStateManager::instance().saveTransferState(state);
        LOG_INFO_FMT("New file transfer started: %1, total chunks: %2", info->fileName, state.totalChunks);
    }

    TransferState currentState = TransferStateManager::instance().loadTransferState(fileId);
    info->session.reset(info->totalChunks, currentState.completedChunks);

    connect(info->thread,&FileTransferThread::chunkReady,this,&FileTransferManager::onChunkReady);
    connect(info->thread,&FileTransferThread::progressUpdated,this,&FileTransferManager::onProgressUpdated);
    connect(info->thread,&FileTransferThread::transferCompleted,this,&FileTransferManager::onTransferCompleted);
    connect(info->thread,&FileTransferThread::transferFailed,this,&FileTransferManager::onTransferFailed);
    connect(info->thread,&FileTransferThread::transferPaused,this,&FileTransferManager::onTransferPaused);

    m_transfers[fileId] = info;

    emit transferStarted(fileId,info->fileName);

    dispatchTransfer(info);
    if(!m_dispatchTimer->isActive()){
        m_dispatchTimer->start();
    }

    return fileId;
}

void FileTransferManager::pauseTransfer(const QString &fileId)
{
    if(!m_transfers.contains(fileId)){
        return;
    }

    FileTransferInfo *info = m_transfers[fileId];
    if(info->thread){
        info->thread->pauseTransfer();
        TransferStateManager::instance().flush();
        LOG_INFO_FMT("File transfer paused:%1",fileId);
    }
}

void FileTransferManager::resumeTransfer(const QString &fileId)
{
    if(!m_transfers.contains(fileId)){
        TransferState state = TransferStateManager::instance().loadTransferState(fileId);
        if(state.fileId.isEmpty() || !state.isSending){
            LOG_WARN_FMT("No transfer state found for:%1",fileId);
            return;
        }

        startSendFile(state.fileId,state.filePath,state.friendId);
        return;
    }

    FileTransferInfo *info = m_transfers[fileId];
    if(info->thread){
        info->thread->resumeTransfer();
        LOG_INFO_FMT("File transfer resumed:%1",fileId);
    }
}

void FileTransferManager::cancelTransfer(const QString &fileId)
{
    if(!m_transfers.contains(fileId)){
        return;
    }

    FileTransferInfo *info = m_transfers[fileId];
    if(info->thread){
        info->thread->stopTransfer();
        info->thread->wait();
        delete info->thread;
    }

    m_transfers.remove(fileId);
    TransferStateManager::instance().removeTransferState(fileId);
    TransferStateManager::instance().flush();
    delete info;
    LOG_INFO_FMT("File transfer canceled:%1",fileId);
}

FileTransferInfo *FileTransferManager::getTransferInfo(const QString &fileId)
{
    return m_transfers.value(fileId,nullptr);
}

QList<TransferState> FileTransferManager::getIncompleteTransfers()
{
    return TransferStateManager::instance().getIncompleteTransfers();
}

void FileTransferManager::onChunkAcked(const QString &fileId, int chunkIndex)
{
    FileTransferInfo *info = m_transfers.value(fileId, nullptr);
    if (!info) {
        return;
    }

    if (chunkIndex >= info->totalChunks) {
        LOG_WARN(QString("Ignore invalid ACK: fileId=%1 chunk=%2 total=%3")
                 .arg(fileId).arg(chunkIndex).arg(info->totalChunks));
        return;
    }

    TransferState state = TransferStateManager::instance().loadTransferState(fileId);
    if (state.fileId.isEmpty()) {
        LOG_WARN_FMT("Ignore ACK without transfer state: %1", fileId);
        return;
    }

    if (chunkIndex >= 0) {
        if (!info->session.markAcked(chunkIndex)) {
            return;
        }
        TransferStateManager::instance().markChunkCompleted(fileId, chunkIndex);
        state = TransferStateManager::instance().loadTransferState(fileId);
    }

    const int totalChunks = state.totalChunks > 0 ? state.totalChunks : info->totalChunks;
    const int ackedChunks = info->session.ackedCount();

    info->progress = totalChunks > 0 ? (ackedChunks * 100) / totalChunks : 0;
    emit transferProgress(fileId, info->progress, qMin<qint64>(info->fileSize, (qint64)ackedChunks * 64 * 1024), info->fileSize);

    if (info->session.isComplete() || (info->allChunksSent && totalChunks > 0 && ackedChunks >= totalChunks)) {
        TransferStateManager::instance().removeTransferState(fileId);
        TransferStateManager::instance().flush();
        emit transferCompleted(fileId);

        QTimer::singleShot(1000,this,[=](){
            if(m_transfers.contains(fileId)){
                FileTransferInfo *currentInfo = m_transfers[fileId];
                if(currentInfo->thread){
                    currentInfo->thread->wait();
                    delete currentInfo->thread;
                }
                m_transfers.remove(fileId);
                delete currentInfo;
            }
        });
    }

    dispatchTransfer(info);
}

void FileTransferManager::dispatchPendingChunks()
{
    const auto transfers = m_transfers.values();
    for(FileTransferInfo *info : transfers){
        dispatchTransfer(info);
    }

    bool hasActiveTransfer = false;
    for(FileTransferInfo *info : m_transfers.values()){
        if(info && !info->session.isComplete()){
            hasActiveTransfer = true;
            break;
        }
    }
    if(!hasActiveTransfer){
        m_dispatchTimer->stop();
    }
}

void FileTransferManager::dispatchTransfer(FileTransferInfo *info)
{
    if(!info || !info->thread){
        return;
    }

    constexpr int kMaxInFlightChunks = 8;
    constexpr int kAckTimeoutMs = 3000;
    constexpr int kMaxRetryAttempts = 5;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for(int chunkIndex : info->session.timedOutChunks(nowMs, kAckTimeoutMs, kMaxRetryAttempts)){
        sendChunk(info, chunkIndex, true);
    }

    for(int chunkIndex : info->session.takeSendWindow(kMaxInFlightChunks)){
        sendChunk(info, chunkIndex, false);
    }

    info->allChunksSent = info->session.inFlightCount() > 0
                          && info->session.ackedCount() + info->session.inFlightCount() >= info->totalChunks;
}

void FileTransferManager::sendChunk(FileTransferInfo *info, int chunkIndex, bool isRetry)
{
    QString error;
    int totalChunks = 0;
    const QByteArray chunk = info->thread->readEncryptedChunk(chunkIndex, &totalChunks, &error);
    if(chunk.isEmpty()){
        info->session.markFailed();
        emit transferFailed(info->fileId, error.isEmpty() ? "读取文件分片失败" : error);
        cancelTransfer(info->fileId);
        return;
    }

    info->session.markSent(chunkIndex, QDateTime::currentMSecsSinceEpoch());
    emit sendFileChunk(info->fileId, chunk, chunkIndex, totalChunks, info->friendId);

    if(isRetry){
        LOG_WARN(QString("Retry file chunk: fileId=%1 chunk=%2").arg(info->fileId).arg(chunkIndex));
    }
}

void FileTransferManager::onChunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks)
{
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    QString fileId;
    int friendId = 0;

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            fileId = it.key();
            friendId = it.value()->friendId;
            break;
        }
    }

    if(!fileId.isEmpty()){
        emit sendFileChunk(fileId,chunk,chunkIndex,totalChunks,friendId);
    }
}

void FileTransferManager::onProgressUpdated(int percent, qint64 sent, qint64 total)
{
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            it.value()->progress = percent;
            emit transferProgress(it.key(),percent,sent,total);
            break;
        }
    }
}

void FileTransferManager::onTransferCompleted()
{
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            QString fileId = it.key();
            it.value()->allChunksSent = true;

            TransferState state = TransferStateManager::instance().loadTransferState(fileId);
            if (!state.fileId.isEmpty()
                && state.totalChunks > 0
                && state.completedChunks.size() >= state.totalChunks) {
                onChunkAcked(fileId, -1);
            } else {
                LOG_INFO_FMT("All chunks sent for %1, waiting for receiver ACKs", fileId);
            }
            break;
        }
    }
}

void FileTransferManager::onTransferFailed(const QString &error)
{
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            QString fileId = it.key();
            emit transferFailed(fileId,error);
            cancelTransfer(fileId);
            break;
        }
    }
}

void FileTransferManager::onTransferPaused(int lastChunkIndex)
{
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            QString fileId = it.key();
            emit transferPaused(fileId,lastChunkIndex);
            LOG_INFO_FMT("File transfer paused:%1",fileId);
            break;
        }
    }
}
