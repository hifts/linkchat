#include "filereceiver.h"

#include <QCoreApplication>
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
    // 确定保存路径,程序同目录
    QString downloadPath = QCoreApplication::applicationDirPath() + "/ReceivedFiles";

    QDir().mkpath(downloadPath);
    QString savePath = downloadPath + "/" + fileName;

    // 如果文件已存在，添加序号
    int counter = 1;
    while (QFile::exists(savePath)) {
        QFileInfo fi(fileName);
        QString baseName = fi.completeBaseName();
        QString ext = fi.suffix();
        savePath = downloadPath + "/" + baseName + QString("(%1).").arg(counter) + ext;
        counter++;
    }

    // 创建文件
    QFile *file = new QFile(savePath);
    if(!file->open(QIODevice::WriteOnly)){
        qCritical() << "[FileReceiver] Failed to create file:" << savePath;
        delete file;
        emit receiveFailed(fileId, "无法创建文件");
        return false;
    }

    // 创建接收文件信息
    ReceivingFileInfo *info = new ReceivingFileInfo;
    info->fileId = fileId;
    info->fileName = fileName;
    info->savePath = savePath;
    info->totalSize = fileSize;
    info->totalChunks = (fileSize + 64 * 1024 -1) / (64 * 1024);
    info->receivedChunks = 0;
    info->file = file;

    // 保存文件传输任务
    m_receivingFiles[fileId] = info;

    return true;
}

bool FileReceiver::receiveChunk(const QString &fileId, int chunkIndex, const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);

    // 检查任务是否存在(不存在则不能接收数据)
    if (!m_receivingFiles.contains(fileId)) {
        qWarning() << "[FileReceiver] File not found:" << fileId;
        return false;
    }

    ReceivingFileInfo *info = m_receivingFiles[fileId];

    // 检查分片是否已经接收
    if(info->receivedChunkMap.contains(chunkIndex)){
        qDebug() << "[FileReceiver] Chunk already received:" << chunkIndex;
        return true; // 重复分片，直接忽略
    }

    // 写入文件
    quint64 offset = static_cast<quint64>(chunkIndex) * 64 * 1024;
    if(!info->file->seek(offset)){
        qCritical() << "[FileReceiver] Failed to write chunk:" << chunkIndex;
        emit receiveFailed(fileId,"文件定位失败");
        cancelReceiving(fileId);
        return false;
    }

    quint64 written = info->file->write(data);
    if(written != data.size()){
        // 写入字节数不等于实际字节数时
        qCritical() << "[FileReceiver] Failed to write chunk:" << chunkIndex;
        emit receiveFailed(fileId, "写入文件失败");
        cancelReceiving(fileId);
        return false;
    }

    // 更新接收信息
    info->receivedChunkMap[chunkIndex] = true;
    info->receivedChunks++;
    info->receivedSize += data.size();

    // 计算进度(收到的/总共的)
    int percent = info->totalChunks > 0 ? (info->receivedChunks * 100) / info->totalChunks : 0;

    // 每接收10个分片打印一次日志
    if (chunkIndex % 10 == 0) {
        qDebug() << "[FileReceiver] Received chunk" << chunkIndex
                 << "/" << info->totalChunks << "(" << percent << "%)";
    }

    emit receiveProgress(fileId,percent,info->receivedSize,info->totalSize);

    // 检查是否接收完成
    if(info->receivedChunks >= info->totalChunks){
        info->file->flush();
        info->file->close();

        QString savePath = info->savePath;
        qDebug() << "[FileReceiver] File receive completed:" << savePath;

        emit receiveCompleted(fileId,savePath);

        // 清理接收任务
        delete info->file;
        delete info;
        m_receivingFiles.remove(fileId);
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

        // 删除未接收完的文件
        QFile::remove(info->savePath);
        delete info->file;
    }

    delete info;
    m_receivingFiles.remove(fileId);
}

ReceivingFileInfo *FileReceiver::getReceivingInfo(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);
    return m_receivingFiles.value(fileId,nullptr);
}


