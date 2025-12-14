#ifndef RECONNECTTRANSFERMANAGER_H
#define RECONNECTTRANSFERMANAGER_H

#include <QObject>
#include <QMap>
#include <QTimer>

/**
 * @brief 断线重连传输管理器
 * 负责管理断线重连后的文件传输状态
 */

struct PendingTransferResume{
    QString fileId;
    QString fileName;
    int friendId;
    bool isSending;
    qint64 timestamp;   // 记录断线时间
};

class ReconnectTransferManager : public QObject
{
    Q_OBJECT
public:
    static ReconnectTransferManager& instance();

    // 记录断线时正在进行的传输
    void saveActiveTransfer(const QString &fileId,const QString &fileName,int friendId,bool isSending);

    // 移除已完成的传输
    void removeCompletedTransfer(const QString &fileId);

    // 网络断线时调用
    void onNetworkDisconnected();

    // 网络重连后调用
    void onNetworkReconnected();

    // 获取所有待恢复的传输
    QList<PendingTransferResume> getPendingTransfers() const;

    // 清理所有待恢复记录
    void clearPendingResumes();

    // 设置自动恢复延迟时间
    void setAutoResumeDelay(int ms);
signals:
    // 准备恢复传输信号
    void readyToResumeTransfer(const QList<PendingTransferResume> &transfers);

    // 单个传输恢复请求
    void requestResumeTransfer(const QString &fileId,int friendId,bool isSending);
private:
    explicit ReconnectTransferManager(QObject *parent = nullptr);
    ~ReconnectTransferManager();

    // 延迟恢复传输
    void scheduleResumeTransfers();

    // 实际执行恢复
    void resumeTransfers();

private:
    QMap<QString, PendingTransferResume> m_activeTransfers; // 当前活动的传输
    QList<PendingTransferResume> m_pengingResumes;          // 待恢复的传输
    QTimer *m_resumeTimer;                                  // 延迟恢复定时器
    int m_resumeDelay;                                      // 恢复延迟时间
    bool m_isConnected;                                     // 当前连接状态
};

#endif // RECONNECTTRANSFERMANAGER_H
