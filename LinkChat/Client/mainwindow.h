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

    // 设置当前用户ID和用户名
    void setCurrentUserId(int newCurrentUserId);
    void setCurrentUserName(const QString &name);

protected:
    // 重写鼠标事件
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:

    // 好友请求
    struct FriendRequest {
        int requesterId;
        QString requesterName;
    };

    // 鼠标位置
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
    
    // 事件过滤器，用于处理子控件的鼠标事件
    bool eventFilter(QObject *watched, QEvent *event) override;
    
    // 回复好友请求
    void sendFriendResponse(int requesterId, bool accepted);

    // 删除好友请求
    void removeRequestAndRefresh(int requesterId);

    // 发送文件传输请求（用于断点续传恢复）
    void sendFileTransferRequestForResume(const QString &fileId, const TransferState &state,int friendId);

    // 检查未完成的传输任务
    void checkIncompleteTransfers();

private slots:
    // 处理收到的好友列表信号
    void onFriendListReceived(QList<FriendInfo> list);

    // 处理好友列表点击信号（选择好友聊天）
    void onContactListClicked(QListWidgetItem *item);

    // 处理鼠标点击信号
    void onContactListPressed(const QModelIndex &index);

    // 处理收到好友的信息信号
    void onSigMsgReceived(uint32_t srcId,QByteArray body);

    void onSigChatHistoryReceived(int friendId, const QList<QPair<int,QByteArray>>& history);

    // 处理好友状态改变信息
    void onSigFriendStatusChanged(int uid, int status);

    // 处理搜索框搜索信息
    void onSearchTextChanged(const QString &text);

    // 定时器结束发送搜索请求
    void onSearchTimerTimeout();

    // 处理搜索结果
    void onSigSearchUserResult(QList<FriendInfo> list);

    // 处理好友请求
    void onSigFriendRequestReceived(int uid,const QString name);

    // 处理好友同意
    void onSigFriendRequestAccepted();

    // 处理好友拒绝
    void onSigFriendRequestRejected();

    // 刷新好友请求
    void updateNewFriendsPage();

    // 处理文件发送请求信号
    void onSigFileTransferRequest(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId);

    // 处理接收方是否接收文件信号
    void onsigFileTransferResponse(const QString &fileId,bool accepted);

    // 处理文件开始传输
    void onFileTransferStarted(const QString &fileId, const QString &fileName);

    // 处理传输进度
    void onFileTransferProgress(const QString &fileId, int percent, qint64 sent, qint64 total);

    // 处理文件传输完成
    void onFileTransferCompleted(const QString &fileId);

    // 处理文件传输失败
    void onFileTransferFailed(const QString &fileId, const QString &error);

    // 处理文件传输暂停
    void onFileTransferPaused(const QString &fileId, int lastChunkIndex);

    // 处理发送分片
    void onSendFileChunk(const QString &fileId, const QByteArray &chunk, int chunkIndex, int totalChunks, int friendId);

    // 处理接收文件信号
    void onFileReceiveChunk(const QString &fileId,int chunkIndex,const QByteArray &chunk);
    void onFileReceiveProgress(const QString &fileId, int percent, qint64 received, qint64 total);
    void onFileReceiveCompleted(const QString &fileId,const QString &savePath);
    void onFileReceiveFailed(const QString &fileId, const QString &error);

    // 处理群聊信号
    void onGroupListReceived(QList<GroupInfo> list);
    void onGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body);
    void onGroupChatHistoryReceived(int groupId, const QList<std::tuple<int, QString, QByteArray>>& history);
    void onCreateGroupResult(bool success, int groupId);
    void onInviteToGroupNotify(int groupId, const QString &groupName, int inviterId, const QString &inviterName);

    // 处理重连信号
    void onConnectionStateChanged(bool connected);
    void onReconnectStateChanged(int attempts, int delayMs);
    void onMaxAttemptsReached();

    // 处理断线重连传输相关信号
    void onReadyToResumeTransfers(const QList<PendingTransferResume> &transfers);
    void onRequestResumeTransfer(const QString &fileId, int friendId,bool isSending);

    // 处理断点续传协议信号
    void onFileResumeReq(const QString &fileId, int senderId);
    void onFileResumeResp(const QString &fileId, bool canResume, int totalChunks, int receivedChunks, const QByteArray &bitmap);

    void on_btnSend_clicked();

    void on_btnContact_clicked();

    void on_btnNewFriends_clicked();

    void on_btnImage_clicked();

    void on_btnFile_clicked();

    void on_btnGroup_clicked();

private:
    Ui::MainWindow *ui;

    QPoint m_dragPosition;                  // 记录鼠标按下时的相对位置
    ResizeDirection m_resizeDir = None;
    QPoint m_lastPos;
    const int m_resizeBorderWidth = 6;      // 边缘6像素内可触发resize

    int m_currentFriendId = 0;              // 记录当前正在跟谁聊天
    QSet<int> m_friendIds;                  // 记录当前用户的好友id
    int m_currentUserId = 0;                // 当前登录用户的ID（登录成功时保存）

    QTimer *m_searchTimer;                  // 防抖定时器

    ChatModel *m_chatModel;
    ChatDelegate *m_chatDelegate;
    ContactDelegate *m_contactDelegate;

    // Key是好友ID，Value是聊天记录列表
    QMap<int,QList<ChatMessage>> m_chatHistory;

    // 待处理的好友申请
    QList<FriendRequest> m_pendingRequests;

    // 用于暂存等待对方同意的文件传输
    QMap<QString, QString> m_pendingFileTransfers;

    int m_currentGroupId = 0;               // 当前群聊ID（0表示私聊模式）
    bool m_isGroupChat = false;             // 当前是否在群聊模式
    QMap<int, QList<ChatMessage>> m_groupChatHistory;  // 群聊记录
    QSet<int> m_groupIds;                   // 用户加入的群ID集合
    QString m_currentUserName;              // 当前用户用户名
    QList<int> m_pendingGroupMembers;       // 创建群时待邀请的成员列表
};

#endif // MAINWINDOW_H
