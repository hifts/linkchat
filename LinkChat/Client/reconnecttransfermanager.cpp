#include "reconnecttransfermanager.h"
#include "transferstatemanager.h"
#include "logger.h"

#include <QDateTime>

ReconnectTransferManager &ReconnectTransferManager::instance()
{
    static ReconnectTransferManager instance;
    return instance;
}

ReconnectTransferManager::ReconnectTransferManager(QObject *parent)
    : QObject{parent}
    , m_resumeDelay(2000)
    , m_isConnected(true)
{
    m_resumeTimer = new QTimer(this);
    m_resumeTimer->setSingleShot(true);
    connect(m_resumeTimer,&QTimer::timeout,this,&ReconnectTransferManager::resumeTransfers);
}

ReconnectTransferManager::~ReconnectTransferManager()
{

}

void ReconnectTransferManager::saveActiveTransfer(const QString &fileId, const QString &fileName, int friendId, bool isSending)
{
    PendingTransferResume resume;
    resume.fileId = fileId;
    resume.fileName = fileName;
    resume.friendId = friendId;
    resume.isSending = isSending;

    resume.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_activeTransfers[fileId] = resume;
}

void ReconnectTransferManager::removeCompletedTransfer(const QString &fileId)
{
    if(m_activeTransfers.contains(fileId)){
        m_activeTransfers.remove(fileId);
    }
}

void ReconnectTransferManager::onNetworkDisconnected()
{
    if(!m_isConnected){
        return;
    }

    m_isConnected = false;
    LOG_WARN("Network disconnected");

    m_pengingResumes.clear();

    for (auto it = m_activeTransfers.begin(); it != m_activeTransfers.end(); ++it) {
        TransferState state = TransferStateManager::instance().loadTransferState(it.key());
        if(!state.fileId.isEmpty()){
            m_pengingResumes.append(it.value());
        }
    }

    if(!m_pengingResumes.isEmpty()){
        // Pending transfers will be resumed
    }

}

void ReconnectTransferManager::onNetworkReconnected()
{
    if(m_isConnected){
        return;
    }

    m_isConnected = true;

    LOG_INFO("Network reconnected, scheduling transfer resume");

    if(!m_pengingResumes.isEmpty()){
        scheduleResumeTransfers();
    }else{
        LOG_INFO("No pending transfers to resume");
    }

}

QList<PendingTransferResume> ReconnectTransferManager::getPendingTransfers() const
{
    return m_pengingResumes;
}

void ReconnectTransferManager::clearPendingResumes()
{
    m_pengingResumes.clear();
    LOG_INFO("All pending transfers cleared");
}

void ReconnectTransferManager::setAutoResumeDelay(int ms)
{
    m_resumeDelay = ms;
}

void ReconnectTransferManager::scheduleResumeTransfers()
{
    if(m_resumeTimer->isActive()){
        m_resumeTimer->stop();
    }

    LOG_INFO_FMT("Scheduling transfer resume in %1 ms", m_resumeDelay);
    m_resumeTimer->start(m_resumeDelay);
}

void ReconnectTransferManager::resumeTransfers()
{
    if(m_pengingResumes.isEmpty()){
        return;
    }

    emit readyToResumeTransfer(m_pengingResumes);

    for(const PendingTransferResume &resume : m_pengingResumes){
        TransferState state = TransferStateManager::instance().loadTransferState(resume.fileId);
        if(state.fileId.isEmpty()){
            continue;
        }

        if(TransferStateManager::instance().isTransferComplete(resume.fileId)){
            TransferStateManager::instance().removeTransferState(resume.fileId);
            continue;
        }

        emit requestResumeTransfer(resume.fileId, resume.friendId, resume.isSending);
    }

    m_pengingResumes.clear();
}
