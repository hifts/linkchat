#include "transferstatemanager.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

TransferStateManager &TransferStateManager::instance()
{
    static TransferStateManager instance;
    return instance;
}

TransferStateManager::TransferStateManager(QObject *parent)
    : QObject{parent}
{
    // 创建状态文件目录
    QString appPath = QCoreApplication::applicationDirPath();
    QString stateDir = appPath + "/TransferState";
    QDir().mkpath(stateDir);
    m_stateFilePath = getStateFilePath();

    // 程序启动时加载已保存的传输状态
    loadFromFile();

    // 清理过期的传输状态
    cleanupOldTransfers();
}

TransferStateManager::~TransferStateManager()
{
    // 程序退出时保存状态
    QMutexLocker locker(&m_mutex);
    saveToFile();
}

void TransferStateManager::saveTransferState(const TransferState &state)
{
    QMutexLocker locker(&m_mutex);

    TransferState newState = state;
    newState.timestamp = QDateTime::currentSecsSinceEpoch();

    m_transfers[state.fileId] = newState;

    // 保存到文件
    saveToFile();
    LOG_INFO(QString("Saved transfer state for file: %1").arg(state.fileId));
}

TransferState TransferStateManager::loadTransferState(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);
    return m_transfers.value(fileId,TransferState());
}

void TransferStateManager::removeTransferState(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    if(m_transfers.remove(fileId)){
        // 如果删除成功
        saveToFile();
        LOG_INFO(QString("Removed transfer state for file: %1").arg(fileId));
    }else {
        LOG_WARN("Remove transfer state failed");
    }
}

void TransferStateManager::markChunkCompleted(const QString &fileId, int chunkIndex)
{
    QMutexLocker locker(&m_mutex);

    if(m_transfers.contains(fileId)){
        // 存放已完成的分片索引
        m_transfers[fileId].completedChunks.insert(chunkIndex);
        m_transfers[fileId].timestamp = QDateTime::currentSecsSinceEpoch();
    }

    // 每完成10个分片保存一个，避免频繁IO
    if(chunkIndex % 10 == 0){
        saveToFile();
    }
}

QSet<int> TransferStateManager::getCompletedChunks(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    if(m_transfers.contains(fileId)){
        return m_transfers[fileId].completedChunks;
    }
    return QSet<int>();
}

bool TransferStateManager::isTransferComplete(const QString &fileId)
{
    QMutexLocker locker(&m_mutex);

    if(m_transfers.contains(fileId)){
        const TransferState &state = m_transfers[fileId];
        return state.completedChunks.size() >= state.totalChunks;
    }
    return false;
}

QList<TransferState> TransferStateManager::getIncompleteTransfers()
{
    QMutexLocker locker(&m_mutex);
    QList<TransferState> incompleteTransfers;
    for(const TransferState &state : m_transfers.values()){
        if(state.completedChunks.size() < state.totalChunks){
            incompleteTransfers.append(state);
        }
    }
    return incompleteTransfers;
}

void TransferStateManager::cleanupOldTransfers()
{
    QMutexLocker locker(&m_mutex);
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    quint64 sevenDays = 7 * 24 * 60 * 60;

    QStringList oldFiles;
    for(const TransferState &state : m_transfers.values()){
        if(currentTime - state.timestamp > sevenDays){
            oldFiles.append(state.fileId);
        }
    }

    for(const QString &fileId : oldFiles){
        m_transfers.remove(fileId);
        LOG_INFO(QString("Removed old transfer state for file: %1").arg(fileId));
    }

    if(!oldFiles.isEmpty()){
        saveToFile();
    }
}

void TransferStateManager::saveToFile()
{
    // 注意：此方法在已持有锁的情况下被调用，不要再加锁

    QJsonArray transferStates;
    for(const TransferState &state : m_transfers.values()){
        QJsonObject stateObj;
        stateObj["fileId"] = state.fileId;
        stateObj["fileName"] = state.fileName;
        stateObj["filePath"] = state.filePath;
        stateObj["tempFilePath"] = state.tempFilePath;
        stateObj["fileMD5"] = state.fileMD5;
        stateObj["fileSize"] = (qint64)state.fileSize;
        stateObj["friendId"] = state.friendId;
        stateObj["isSending"] = state.isSending;
        stateObj["totalChunks"] = state.totalChunks;
        stateObj["timestamp"] = (qint64)state.timestamp;

        // 保存已完成的分片索引
        QJsonArray completedChunksArray;
        for(int chunkIndex : state.completedChunks){
            completedChunksArray.append(chunkIndex);
        }

        stateObj["completedChunks"] = completedChunksArray;
        transferStates.append(stateObj);
    }

    QJsonDocument doc(transferStates);
    QFile file(m_stateFilePath);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)){
        file.write(doc.toJson());
        file.close();
    }else{
        LOG_ERROR_FMT("Failed to save transfer states to file:%1",m_stateFilePath);
    }
}

void TransferStateManager::loadFromFile()
{
    QFile file(m_stateFilePath);
    if(!file.exists()){
        return;
    }

    // 打开文件
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        LOG_ERROR_FMT("Failed to load transfer states from file:%1",m_stateFilePath);
        return;
    }

    // 读取文件内容
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if(doc.isNull()){
        return;
    }

    // 解析文件内容
    QJsonArray transferStates = doc.array();
    for(const QJsonValue &value : transferStates){
        if(!value.isObject()){
            continue;
        }

        QJsonObject stateObj = value.toObject();
        TransferState state;
        state.fileId = stateObj["fileId"].toString();
        state.fileName = stateObj["fileName"].toString();
        state.filePath = stateObj["filePath"].toString();
        state.tempFilePath = stateObj["tempFilePath"].toString();
        state.fileMD5 = stateObj["fileMD5"].toString();
        state.fileSize = stateObj["fileSize"].toVariant().toLongLong();
        state.friendId = stateObj["friendId"].toInt();
        state.isSending = stateObj["isSending"].toBool();
        state.totalChunks = stateObj["totalChunks"].toInt();
        state.timestamp = stateObj["timestamp"].toVariant().toLongLong();

        // 解析已完成的分片索引
        QJsonArray completedChunksArray = stateObj["completedChunks"].toArray();
        for(const QJsonValue &chunkValue : completedChunksArray){
            state.completedChunks.insert(chunkValue.toInt());
        }
        m_transfers[state.fileId] = state;
    }
    LOG_INFO_FMT("Loaded %1 transfer states from file",m_transfers.size());
}

QString TransferStateManager::getStateFilePath() const
{
    QString appPath = QCoreApplication::applicationDirPath();
    return appPath + "/TransferState/transfer_states.json";
}
