#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "chatdelegate.h"
#include "chatmodel.h"
#include "packet.h"
#include "contactdelegate.h"
#include "transferstatemanager.h"
#include "reconnecttransfermanager.h"

#include <QListWidget>
#include <QMouseEvent>
#include <QWidget>
#include <QMenu>
#include <QMessageBox>
#include <tuple>

namespace Ui {
class MainWindow;
}

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setCurrentUserId(int newCurrentUserId);
    void setCurrentUserName(const QString &name);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    
    void closeEvent(QCloseEvent *event) override;

private:

    struct FriendRequest {
        int requesterId;
        QString requesterName;
    };

    enum ResizeDirection {
        None,
        Top, Bottom, Left, Right,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    ResizeDirection getResizeDirection(const QPoint &pos);
    void updateCursorShape(const QPoint &pos);

private:

    void initUI();
    void initModel();
    void connectSignalsAndSlots();
    
    bool eventFilter(QObject *watched, QEvent *event) override;
    
    void sendFriendResponse(int requesterId, bool accepted);

    void removeRequestAndRefresh(int requesterId);

    void sendFileTransferRequestForResume(const QString &fileId, const TransferState &state,int friendId);

    void checkIncompleteTransfers();
    
    void updateContactLastMsgTime(int friendId, const QDateTime &time);
    
    void updateContactListMode(bool isSessionMode);
    
    void updateNewFriendsButtonState();

private slots:
    void onFriendListReceived(QList<FriendInfo> list);

    void onContactListClicked(QListWidgetItem *item);

    void onContactListPressed(const QModelIndex &index);

    void onSigMsgReceived(uint32_t srcId,QByteArray body);

    void onSigChatHistoryReceived(int friendId, const QList<std::tuple<int, QByteArray, quint64>>& history);

    void onSigFriendStatusChanged(int uid, int status);

    void onSearchTextChanged(const QString &text);

    void onSearchTimerTimeout();

    void onSigSearchUserResult(QList<FriendInfo> list);

    void onSigFriendRequestReceived(int uid,const QString name);

    void onSigFriendRequestAccepted();

    void onSigFriendRequestRejected();

    void onSigDeleteFriendResponse(int result, int targetId);

    void onContactListContextMenu(const QPoint &pos);

    void onSigLeaveGroupResponse(int result, int groupId);

    void updateNewFriendsPage();

    void onSigFileTransferRequest(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId);

    void onsigFileTransferResponse(const QString &fileId,bool accepted);

    void onFileTransferStarted(const QString &fileId, const QString &fileName);

    void onFileTransferProgress(const QString &fileId, int percent, qint64 sent, qint64 total);

    void onFileTransferCompleted(const QString &fileId);

    void onFileTransferFailed(const QString &fileId, const QString &error);

    void onFileTransferPaused(const QString &fileId, int lastChunkIndex);

    void onSendFileChunk(const QString &fileId, const QByteArray &chunk, int chunkIndex, int totalChunks, int friendId);

    void onFileReceiveChunk(const QString &fileId,int chunkIndex,const QByteArray &chunk);
    void onFileReceiveProgress(const QString &fileId, int percent, qint64 received, qint64 total);
    void onFileReceiveCompleted(const QString &fileId,const QString &savePath);
    void onFileReceiveFailed(const QString &fileId, const QString &error);

    void onGroupListReceived(QList<GroupInfo> list);
    void onGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body);
    void onGroupChatHistoryReceived(int groupId, const QList<std::tuple<int, QString, QByteArray, quint64>>& history);
    void onCreateGroupResult(bool success, int groupId);
    void onInviteToGroupNotify(int groupId, const QString &groupName, int inviterId, const QString &inviterName);

    void onConnectionStateChanged(bool connected);
    void onReconnectStateChanged(int attempts, int delayMs);
    void onMaxAttemptsReached();

    void onReadyToResumeTransfers(const QList<PendingTransferResume> &transfers);
    void onRequestResumeTransfer(const QString &fileId, int friendId,bool isSending);

    void onFileResumeReq(const QString &fileId, int senderId);
    void onFileResumeResp(const QString &fileId, bool canResume, int totalChunks, int receivedChunks, const QByteArray &bitmap);

    void on_btnSend_clicked();

    void on_btnChat_clicked();

    void on_btnContact_clicked();

    void on_btnNewFriends_clicked();

    void on_btnImage_clicked();

    void on_btnFile_clicked();

    void on_btnGroup_clicked();

private:
    Ui::MainWindow *ui;

    QPoint m_dragPosition;
    ResizeDirection m_resizeDir = None;
    QPoint m_lastPos;
    const int m_resizeBorderWidth = 6;

    int m_currentFriendId = 0;
    QSet<int> m_friendIds;
    int m_currentUserId = 0;

    QTimer *m_searchTimer;

    ChatModel *m_chatModel;
    ChatDelegate *m_chatDelegate;
    ContactDelegate *m_contactDelegate;

    QMap<int,QList<ChatMessage>> m_chatHistory;
    
    QMap<int, QDateTime> m_lastMsgTime;

    QList<FriendRequest> m_pendingRequests;
    
    bool m_hasUnreadFriendRequests = false;

    QMap<QString, QString> m_pendingFileTransfers;
    QMap<QString, int> m_pendingFileTransferTargets;

    int m_currentGroupId = 0;
    bool m_isGroupChat = false;
    QMap<int, QList<ChatMessage>> m_groupChatHistory;
    QMap<int, QDateTime> m_groupLastMsgTime;
    QSet<int> m_groupIds;
    QString m_currentUserName;
    QList<int> m_pendingGroupMembers;
};

#endif // MAINWINDOW_H
