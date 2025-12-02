#include "filetransfermanager.h"

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
{}

FileTransferManager::~FileTransferManager()
{
    // 清除所有传输任务
    for(auto it = m_transfers.begin();it != m_transfers.end();++it){
        if(it.value()->thread){
            it.value()->thread->stopTransfer();     // 停止传输文件
            it.value()->thread->wait();             // 等待线程结束
            delete it.value()->thread;
        }
        delete it.value();
    }
    m_transfers.clear();
}

QString FileTransferManager::generateFileId(const QString &filePath)
{
    // 使用文件路径 + 时间戳生成唯一ID,哈希加密
    QString data = filePath + QString::number(QDateTime::currentMSecsSinceEpoch());
    QByteArray hash = QCryptographicHash::hash(data.toUtf8(),QCryptographicHash::Md5);
    return QString(hash.toHex());
}

QString FileTransferManager::startSendFile(const QString &fileId,const QString &filePath, int friendId)
{
    QFileInfo fileInfo(filePath);

    // 文件不存在或者是文件夹则不能传输
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "[FileTransferManager] File not found:" << filePath;
        return QString();
    }

    QString fid = fileId;

    // 创建传输信息
    FileTransferInfo *info = new FileTransferInfo;
    info->fileId = fileId;
    info->fileName = fileInfo.fileName();
    info->filePath = filePath;
    info->fileSize = fileInfo.size();
    info->friendId = friendId;
    info->isReceiving = false;
    info->progress = 0;
    info->thread = new FileTransferThread(filePath,fileId,friendId,this);

    // 连接信号（信号会自动跨线程传递）
    connect(info->thread,&FileTransferThread::chunkReady,this,&FileTransferManager::onChunkReady);
    connect(info->thread,&FileTransferThread::progressUpdated,this,&FileTransferManager::onProgressUpdated);
    connect(info->thread,&FileTransferThread::transferCompleted,this,&FileTransferManager::onTransferCompleted);
    connect(info->thread,&FileTransferThread::transferFailed,this,&FileTransferManager::onTransferFailed);

    // connect(info->thread, &QThread::finished, info->thread, &QObject::deleteLater);

    m_transfers[fileId] = info;

    // 发送开始传输信息
    emit transferStarted(fileId,info->fileName);

    // 自动调用run()方法
    info->thread->start();

    return fileId;
}

void FileTransferManager::startReceiveFile(const QString &fileId,
                                           const QString &fileName,
                                           qint64 fileSize,
                                           int friendId)
{
    // 创建接收文件的传输信息
    FileTransferInfo *info = new FileTransferInfo;
    info->fileId = fileId;
    info->fileName = fileName;
    info->fileSize = fileSize;
    info->friendId = friendId;
    info->isReceiving = true;
    info->progress = 0;
    info->thread = nullptr;         // 接收端不需要线程

    // 程序同目录
    QString downloadPath = QCoreApplication::applicationDirPath() + "/ReceivedFiles";
    QDir().mkpath(downloadPath);
    info->filePath = downloadPath + "/" + fileName;

    m_transfers[fileId] = info;
    emit transferStarted(fileId,fileName);
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
    delete info;
    qDebug() << "[FileTransferManager] Canceled:" << fileId;
}

FileTransferInfo *FileTransferManager::getTransferInfo(const QString &fileId)
{
    return m_transfers.value(fileId,nullptr);
}

void FileTransferManager::onChunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks)
{
    // 获取发送该信号的线程对象
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    // 找到对应的传输信息
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
    // 获取发送该信号的线程对象
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
    // 获取发送该信号的线程对象
    FileTransferThread *thread = qobject_cast<FileTransferThread*>(sender());
    if(!thread){
        return;
    }

    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if(it.value()->thread == thread){
            QString fileId = it.key();
            emit transferCompleted(fileId);

            // 延迟清理已经完成传输的线程
            QTimer::singleShot(1000,this,[=](){
                if(m_transfers.contains(fileId)){
                    FileTransferInfo *info = m_transfers[fileId];
                    if(info->thread){
                        info->thread->wait();
                        delete info->thread;
                    }
                    m_transfers.remove(fileId);
                    delete info;
                }
            });
            break;
        }
    }
}

void FileTransferManager::onTransferFailed(const QString &error)
{
    // 获取发送该信号的线程对象
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
