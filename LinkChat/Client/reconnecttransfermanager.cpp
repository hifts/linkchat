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
    , m_resumeDelay(2000)  // 默认2秒延迟
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

    LOG_INFO_FMT("Active transfer saved: %1 (sending: %2) ", fileId, isSending);
}

void ReconnectTransferManager::removeCompletedTransfer(const QString &fileId)
{
    if(m_activeTransfers.contains(fileId)){
        m_activeTransfers.remove(fileId);
        LOG_INFO_FMT("Completed transfer removed: %1", fileId);
    }
}

void ReconnectTransferManager::onNetworkDisconnected()
{
    if(!m_isConnected){
        return;
    }

    m_isConnected = false;
    LOG_WARN("Network disconnected");

    // 将所有活动传输保存到待恢复列表
    m_pengingResumes.clear();

    for (auto it = m_activeTransfers.begin(); it != m_activeTransfers.end(); ++it) {
        // 检查传输状态是否存在
        TransferState state = TransferStateManager::instance().loadTransferState(it.key());
        if(!state.fileId.isEmpty()){
            m_pengingResumes.append(it.value());
            LOG_INFO_FMT("Pending transfer added: %1", it.value().fileId);
        }
    }

    if(!m_pengingResumes.isEmpty()){
        LOG_INFO_FMT("Total %1 transfers will be resumed after reconnection", m_pengingResumes.size());
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
        // 延迟恢复传输
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
        LOG_INFO("No pending transfers to resume");
        return;
    }

    LOG_INFO_FMT("Resuming %1 transfers", m_pengingResumes.size());

    // 发送恢复传输信号
    emit readyToResumeTransfer(m_pengingResumes);

    // 逐个请求恢复
    for(const PendingTransferResume &resume : m_pengingResumes){
        // 检查传输状态
        TransferState state = TransferStateManager::instance().loadTransferState(resume.fileId);
        if(state.fileId.isEmpty()){
            LOG_INFO_FMT("Transfer state not found for %1", resume.fileId);
            continue;
        }

        // 检查是否已经完成
        if(TransferStateManager::instance().isTransferComplete(resume.fileId)){
            LOG_INFO_FMT("Transfer already completed: %1", resume.fileId);
            TransferStateManager::instance().removeTransferState(resume.fileId);
            continue;
        }

        LOG_INFO_FMT("Resuming transfer: %1", resume.fileId);

        // 请求恢复传输
        emit requestResumeTransfer(resume.fileId, resume.friendId, resume.isSending);
    }

    m_pengingResumes.clear();
    LOG_INFO("All pending transfers resumed");
}
