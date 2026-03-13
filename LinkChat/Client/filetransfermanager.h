#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include "filetransferthread.h"
#include "transferstatemanager.h"

#include <QObject>
#include <QMap>


struct FileTransferInfo {
    QString fileId;
    QString fileName;
    QString filePath;
    qint64 fileSize;
    int friendId;
    bool isReceiving;
    int progress;
    FileTransferThread *thread;
};

class FileTransferManager : public QObject
{
    Q_OBJECT
public:
    static FileTransferManager &instance();

    void setCurrentUserId(int userId);

    QString startSendFile(const QString &fileId,const QString &filePath,int friendId);

    void pauseTransfer(const QString &fileId);

    void resumeTransfer(const QString &fileId);

    void cancelTransfer(const QString &fileId);

    FileTransferInfo *getTransferInfo(const QString &fileId);

    QList<TransferState> getIncompleteTransfers();

    QString generateFileId(const QString &filePath);
signals:
    void transferStarted(const QString &fileId, const QString &fileName);
    void transferProgress(const QString &fileId, int percent, qint64 sent, qint64 total);
    void transferCompleted(const QString &fileId);
    void transferFailed(const QString &fileId, const QString &error);
    void transferPaused(const QString &fileId,int lastChunkIndex);

    void sendFileChunk(const QString &fileId, const QByteArray &chunk,
                       int chunkIndex, int totalChunks, int friendId);

private slots:
    void onChunkReady(const QByteArray &chunk, int chunkIndex, int totalChunks);
    void onProgressUpdated(int percent, qint64 sent, qint64 total);
    void onTransferCompleted();
    void onTransferFailed(const QString &error);
    void onTransferPaused(int lastChunkIndex);
private:
    explicit FileTransferManager(QObject *parent = nullptr);
    ~FileTransferManager();



    QMap<QString,FileTransferInfo*> m_transfers;
    int m_currentUserId;
};

#endif // FILETRANSFERMANAGER_H
