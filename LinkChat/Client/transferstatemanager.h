#ifndef TRANSFERSTATEMANAGER_H
#define TRANSFERSTATEMANAGER_H

#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>

/**
 * @brief 传输状态管理器
 * 负责保存和恢复文件传输状态，支持断点续传
 */

struct TransferState {
    QString fileId;             // 文件唯一标识
    QString fileName;           // 文件名
    QString filePath;           // 文件路径
    QString tempFilePath;       // 临时文件路径（接收端使用）
    QString fileMD5;            // 文件MD5校验值
    qint64 fileSize = 0;        // 文件大小
    int friendId = 0;           // 好友ID
    int totalChunks = 0;        // 总分片数
    QSet<int> completedChunks;  // 已完成的分片索引集合
    bool isSending = false;     // true=发送中 false=接收中
    qint64 timestamp = 0;       // 最后更新时间戳
};

class TransferStateManager : public QObject
{
    Q_OBJECT
public:
    static TransferStateManager& instance();

    // 保存传输状态
    void saveTransferState(const TransferState& state);

    // 加载传输状态
    TransferState loadTransferState(const QString& fileId);

    // 删除传输状态
    void removeTransferState(const QString& fileId);

    // 标记分片已完成
    void markChunkCompleted(const QString& fileId, int chunkIndex);

    // 获取已完成的分片列表
    QSet<int> getCompletedChunks(const QString& fileId);

    // 检查传输是否完成
    bool isTransferComplete(const QString& fileId);

    // 获取所有未完成的传输
    QList<TransferState> getIncompleteTransfers();

    // 清理过期的传输状态（超过7天）
    void cleanupOldTransfers();
private:
    explicit TransferStateManager(QObject *parent = nullptr);
    ~TransferStateManager();

    // 持久化到文件
    void saveToFile();

    // 从文件加载
    void loadFromFile();

    // 获取状态文件路径
    QString getStateFilePath() const;

    QMap<QString, TransferState> m_transfers;
    QMutex m_mutex;
    QString m_stateFilePath;
signals:
};

#endif // TRANSFERSTATEMANAGER_H
