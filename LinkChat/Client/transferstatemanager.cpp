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
    QString appPath = QCoreApplication::applicationDirPath();
    QString stateDir = appPath + "/TransferState";
    QDir().mkpath(stateDir);
    m_stateFilePath = getStateFilePath();

    loadFromFile();

    cleanupOldTransfers();
}

TransferStateManager::~TransferStateManager()
{
    QMutexLocker locker(&m_mutex);
    saveToFile();
}

void TransferStateManager::saveTransferState(const TransferState &state)
{
    QMutexLocker locker(&m_mutex);

    TransferState newState = state;
    newState.timestamp = QDateTime::currentSecsSinceEpoch();

    m_transfers[state.fileId] = newState;

    saveToFile();
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
        saveToFile();
    }else {
        LOG_WARN("Remove transfer state failed");
    }
}

void TransferStateManager::markChunkCompleted(const QString &fileId, int chunkIndex)
{
    QMutexLocker locker(&m_mutex);

    if(m_transfers.contains(fileId)){
        m_transfers[fileId].completedChunks.insert(chunkIndex);
        m_transfers[fileId].timestamp = QDateTime::currentSecsSinceEpoch();
    }

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
    }

    if(!oldFiles.isEmpty()){
        saveToFile();
    }
}

void TransferStateManager::saveToFile()
{

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

    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        LOG_ERROR_FMT("Failed to load transfer states from file:%1",m_stateFilePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if(doc.isNull()){
        return;
    }

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

        QJsonArray completedChunksArray = stateObj["completedChunks"].toArray();
        for(const QJsonValue &chunkValue : completedChunksArray){
            state.completedChunks.insert(chunkValue.toInt());
        }
        m_transfers[state.fileId] = state;
    }
}

QString TransferStateManager::getStateFilePath() const
{
    QString appPath = QCoreApplication::applicationDirPath();
    return appPath + "/TransferState/transfer_states.json";
}
