#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "networkmanager.h"

#include <QBuffer>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setMouseTracking(true);

    // 初始化界面
    initUI();

    initModel();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setInterval(300);        // 隔300ms再触发搜索
    m_searchTimer->setSingleShot(true);     // 只触发一次

    connectSignalsAndSlots();

    // 界面出来后刷新好友列表(向服务器请求好友信息)
    // 使用 QTimer::singleShot 0ms 延时，确保构造函数执行完后再发包
    QTimer::singleShot(0,[](){
        // 发送空包即可，因为 Server 知道你是谁
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ,QByteArray());
    });

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    // 只有左键点击才能拖动
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = getResizeDirection(event->pos());
        if (m_resizeDir != None) {
            // 开始调整大小
            m_lastPos = event->globalPos();
            event->accept();
            return;
        }

        // 计算鼠标相对于窗口左上角的偏移量
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();

    // 1. 如果正在调整大小 → 直接 resize
    if (event->buttons() & Qt::LeftButton && m_resizeDir != None) {
        QRect geom = geometry();
        QPoint delta = event->globalPos() - m_lastPos;

        switch (m_resizeDir) {
        case Top:
            geom.setTop(geom.top() + delta.y());
            break;
        case Bottom:
            geom.setBottom(geom.bottom() + delta.y());
            break;
        case Left:
            geom.setLeft(geom.left() + delta.x());
            break;
        case Right:
            geom.setRight(geom.right() + delta.x());
            break;
        case TopLeft:
            geom.setTopLeft(geom.topLeft() + delta);
            break;
        case TopRight:
            geom.setTop(geom.top() + delta.y());
            geom.setRight(geom.right() + delta.x());
            break;
        case BottomLeft:
            geom.setBottom(geom.bottom() + delta.y());
            geom.setLeft(geom.left() + delta.x());
            break;
        case BottomRight:
            geom.setBottomRight(geom.bottomRight() + delta);
            break;
        default:
            break;
        }

        if (geom.width() >= minimumWidth() && geom.height() >= minimumHeight()) {
            setGeometry(geom);
            // 每次 resize 成功后，更新 m_lastPos，防止跳变
            m_lastPos = event->globalPos();
        }
        event->accept();
        return;
    }

    // 2. 如果只是移动鼠标 → 更新光标形状
    updateCursorShape(pos);

    // 只有按住左键且不是调整窗口大小才处理
    if (event->buttons() & Qt::LeftButton && m_resizeDir == None) {
        // 移动窗口到鼠标当前位置减去偏移量
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = None;
        // 释放后恢复箭头光标
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::updateCursorShape(const QPoint &pos)
{
    if (m_resizeDir != None) {
        // 正在拖动时保持当前光标
        return;
    }

    switch (getResizeDirection(pos)) {
    case Top:
    case Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case Left:
    case Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case TopLeft:
    case BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case TopRight:
    case BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

MainWindow::ResizeDirection MainWindow::getResizeDirection(const QPoint &pos)
{
    const int x = pos.x();
    const int y = pos.y();
    const int w = width();
    const int h = height();
    const int b = m_resizeBorderWidth;

    bool left   = x < b;
    bool right  = x >= w - b;
    bool top    = y < b;
    bool bottom = y >= h - b;

    if (left && top)    return TopLeft;
    if (right && top)   return TopRight;
    if (left && bottom) return BottomLeft;
    if (right && bottom)return BottomRight;
    if (left)           return Left;
    if (right)          return Right;
    if (top)            return Top;
    if (bottom)         return Bottom;

    return None;
}

void MainWindow::initUI()
{
    ui->stackedWidget->setCurrentIndex(0);

    // 1. 无边框 + 拖拽逻辑
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // 在 MainWindow 构造函数里，initUI() 之后加上这句：
    ui->chatList->setMouseTracking(true);
    ui->chatList->viewport()->setAttribute(Qt::WA_Hover);

    // 2. 现代深色 UI 样式
    QString style = R"(
    /* 1. 全局设置：字体与去边框 */
    QWidget {
        font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
        font-size: 14px;
        border: none; /* 暴力去除所有默认边框，解决白线问题 */
    }

    /* 2. 左侧侧边栏：最深色 (#202225) */
    QWidget#sideBar {
        background-color: #202225;
        border: none; /* 确保自身无边框 */
    }

    /* 侧边栏通用按钮 */
    QWidget#sideBar QPushButton {
        background-color: transparent;
        color: #b9bbbe;       /* 浅灰色文字 */
        border: none;         /* 去掉按钮边框 */
        outline: none;        /* 【关键】去掉选中时的那个丑陋虚线框！ */
        border-radius: 10px;
        padding: 10px 5px;    /* 上下10px，左右缩减为5px，给文字留空 */
        margin: 5px 8px;      /* 按钮左右留点空隙，不要贴边 */
        font-weight: bold;
        font-size: 10px;      /* 【关键】字体改小，不然放不下 */
        text-align: left;     /* 文字靠左，这样图标和文字会排得整齐 */
    }

    QWidget#sideBar QPushButton:hover {
        background-color: #4f545c; /* 悬停深灰 */
        color: #ffffff;
    }

    QWidget#sideBar QPushButton:checked {
        background-color: #5865F2; /* 选中时变成 Discord 蓝 */
        color: #ffffff;
    }

    /* 头像按钮特殊处理：做大一点，居中 */
    QWidget#sideBar QPushButton#btnAvatar {
        background-color: #5865F2;
        color: white;
        border-radius: 18px;   /* 稍微方一点的圆角 */
        font-size: 18px;       /* 头像的 emoji 可以大一点 */
        margin: 15px 10px 20px 10px; /* 上 右 下 左 (下方拉开距离) */
        min-height: 36px;
        text-align: center;    /* 头像居中显示 */
        padding: 0px;          /* 头像不需要内边距 */
    }

    /* 3. 中间列表区：中灰色 (#2f3136) */
    QWidget#listArea{
        background-color: #2f3136;
    }

    /* 新朋友列表专用样式 */
    QListWidget#friendRequestsList {
        background-color: #2f3136;
        border: none;
        outline: none;
    }
    QListWidget#friendRequestsList::item {
        background-color: transparent;
        border-bottom: 1px solid #3f4147; /* 给每一行加个底部分割线，更好看 */
        padding: 0px; /* 因为我们在 Widget 里处理了 margin */
    }
    QListWidget#friendRequestsList::item:hover {
        background-color: #35373c; /* 悬停时整个条目变亮一点 */
    }
    QListWidget#friendRequestsList::item:selected {
        background-color: transparent; /* 禁止选中变蓝，因为只有按钮是交互点 */
    }

    QLabel#newFriendTitle {
        background-color: #2f3136;
        color: #72767d;
        font-size: 14px;
        font-weight: bold;
    }

    /* 搜索框：更黑一点，圆角 */
    QLineEdit#searchEdit {
        background-color: #202225;
        color: #dcddde;
        border: none;
        border-radius: 4px;
        padding: 8px;
        margin: 10px 10px 0px 10px; /* 上右下左 */
        font-size: 13px;
    }
    QLineEdit#searchEdit:focus {
        background-color: #202225; /* 保持颜色 */
        color: white;
    }

    /* 列表控件 */
    QListWidget {
        background-color: transparent;
        outline: none; /* 去掉选中时的虚线框 */
    }
    QListWidget::item {
        color: #8e9297; /* 默认未选中颜色 */
        padding: 10px;
        border-radius: 5px; /* 列表项也要圆角 */
        margin: 1px 10px; /* 列表项左右留空，做成悬浮卡片感 */
    }
    QListWidget::item:hover {
        background-color: #35373c;
        color: #dcddde;
    }
    QListWidget::item:selected {
        background-color: #393c43;
        color: #ffffff;
    }

    /* 4. 右侧聊天区：最亮色 (#36393f) */
    QWidget#chatArea {
        background-color: #36393f;
    }

    /* 顶部标题栏 */
    QLabel#lblChatTitle {
        color: #72767d;
        font-size: 14px;
        font-weight: bold;
        border-bottom: 1px solid #26272d; /* 分割线 */
    }

    /* 聊天记录列表 */
    QListView#chatList {
        background-color: transparent;
        border: none;
        padding: 10px;
    }

    /* 【修复】底部输入容器 */
    QWidget#inputWidget {
        background-color: #36393f; /* 与背景同色 */
        padding: 10px;
    }

    /* 【修复】输入框 QTextEdit */
    QTextEdit#msgEdit {
        background-color: #40444b; /* 输入框背景深灰 */
        color: white;
        border: none;
        border-radius: 8px;
        padding: 10px;
        font-size: 14px;
    }

    /* 【修复】发送按钮 */
    QPushButton#btnSend {
        background-color: #5865F2; /* 品牌蓝 */
        color: white;
        border: none;
        border-radius: 8px;
        padding: 5px 20px;
        font-weight: bold;
        margin-left: 10px;
    }
    QPushButton#btnSend:hover {
        background-color: #4752c4;
        font-weight: bold;
    }

    QPushButton#btnImage {
        color: #72767d;
        border-radius: 20px;
    }
    QPushButton#btnImage:hover {
        background-color: #40444b;
        font-weight: bold;
    }
)";
    this->setStyleSheet(style);
}

void MainWindow::initModel()
{
    m_chatModel = new ChatModel(this);
    m_chatDelegate = new ChatDelegate(this);
    m_contactDelegate = new ContactDelegate(this);

    // 绑定模型到QListView
    ui->chatList->setModel(m_chatModel);
    ui->chatList->setItemDelegate(m_chatDelegate);

    ui->chatList->setResizeMode(QListView::Adjust);
    ui->chatList->setSpacing(5);

    // 好友列表代理
    ui->contactList->setItemDelegate(m_contactDelegate);

}

void MainWindow::connectSignalsAndSlots()
{
    // 计时器
    connect(m_searchTimer,&QTimer::timeout,this,&MainWindow::onSearchTimerTimeout);

    // 好友列表显示
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendListReceived,this,&MainWindow::onFriendListReceived);

    // 选择好友列表聊天点击信号
    connect(ui->contactList,&QListWidget::pressed,this,&MainWindow::onContactListPressed);

    // 关联聊天信息信号
    connect(&NetworkManager::instance(),&NetworkManager::sigMsgReceived,this,&MainWindow::onSigMsgReceived);

    connect(&NetworkManager::instance(),&NetworkManager::sigChatHistoryReceived,this,&MainWindow::onSigChatHistoryReceived);

    // 关联好友状态更新信号
    connect(&NetworkManager::instance(), &NetworkManager::sigFriendStatusChanged,this,&MainWindow::onSigFriendStatusChanged);

    // 监听搜索框
    connect(ui->searchEdit,&QLineEdit::textChanged,this,&::MainWindow::onSearchTextChanged);

    // 关联搜索结果信号
    connect(&NetworkManager::instance(),&NetworkManager::sigSearchUserResult,this,&MainWindow::onSigSearchUserResult);

    // 关联转发来的好友请求信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestReceived,this,&MainWindow::onSigFriendRequestReceived);

    // 关联同意信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestAccepted,this,&MainWindow::onSigFriendRequestAccepted);

    // 关联拒绝信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestRejected,this,&MainWindow::onSigFriendRequestRejected);

}

void MainWindow::sendFriendResponse(int requesterId, bool accepted)
{
    AddFriendResp resp;
    resp.requesterId = requesterId;
    resp.accepted = accepted;

    NetworkManager::instance().sendMsg(MSG_ADD_FRIEND_RESP,QByteArray((char*)&resp,sizeof(AddFriendResp)));
}

void MainWindow::removeRequestAndRefresh(int requesterId)
{
    for (int i = 0; i < m_pendingRequests.size(); ++i) {
        if(m_pendingRequests[i].requesterId == requesterId){
            m_pendingRequests.removeAt(i);
            break;
        }
    }
    updateNewFriendsPage();
}

void MainWindow::onFriendListReceived(QList<FriendInfo> list)
{
    ui->contactList->clear();
    m_friendIds.clear();

    for(const auto &info : list){

        // 保存好友id
        m_friendIds.insert(info.id);

        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(200,60));

        // 在item里存放用户id和用户名
        item->setData(ContactDelegate::RoleStatus,info.id);
        item->setData(ContactDelegate::RoleName,QString::fromUtf8(info.userName));
        item->setData(ContactDelegate::RoleIsFriend, true);

        QString status = (info.status == 1)? "[在线]" : "[离线]";
        item->setText(QString("%1 %2").arg(status,QString::fromUtf8(info.userName)));

        // 设置头像
        // item->setIcon();

        ui->contactList->addItem(item);
    }
}

void MainWindow::onContactListClicked(QListWidgetItem *item)
{
    int friendId = item->data(ContactDelegate::RoleStatus).toInt();
    QString name = item->data(ContactDelegate::RoleName).toString();
    bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();

    if(friendId <= 0 || friendId == m_currentFriendId){
        return;
    }

    m_currentFriendId = friendId;
    ui->lblChatTitle->setText(name);

    m_chatModel->clearMessages();

    QByteArray body;
    QDataStream ds(&body,QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint32(friendId);

    NetworkManager::instance().sendMsg(MSG_CHAT_HISTORY_REQ,body);

    // 如果点击的是当前已经在聊的好友，就不做处理
    // if(friendId == m_currentFriendId) {
    //     return;
    // }

    // // 如果之前有在跟人聊天(id!=0)，先把那个人的聊天记录存进 Map，前一个人的聊天记录
    // if(m_currentFriendId != 0){
    //     m_chatHistory[m_currentFriendId] = m_chatModel->getMessages();
    // }

    // // 更新当前聊天对象id
    // this->m_currentFriendId = friendId;

    // // 检查之前是否存有新好友聊天记录
    // if(m_chatHistory.contains(friendId)){
    //     // 如果有就直接设置到模型中
    //     m_chatModel->setMessages(m_chatHistory[friendId]);
    // }else{
    //     // 如果没有，第一次聊天，清空模型
    //     m_chatModel->clearMessages();
    // }

    item->setData(ContactDelegate::RoleUnread, 0);
    ui->chatList->scrollToBottom();

    if (isFriend) {
        // 是好友：启用输入框和按钮，恢复颜色
        ui->lblChatTitle->setText(name);
        ui->msgEdit->setEnabled(true);
        ui->msgEdit->setPlaceholderText(""); // 清空提示
        ui->btnSend->setEnabled(true);
        // 恢复发送按钮的蓝色
        ui->btnSend->setStyleSheet(
            "QPushButton { background-color: #5865F2; color: white; border-radius: 8px; padding: 5px 20px; font-weight: bold; }"
            "QPushButton:hover { background-color: #4752c4; }"
            );
    } else {
        // 不是好友：禁用输入框和按钮，变灰
        ui->lblChatTitle->setText("选择一个好友开始聊天");
        ui->msgEdit->setEnabled(false);
        ui->btnSend->setEnabled(false);
        // 设置发送按钮为灰色
        ui->btnSend->setStyleSheet(
            "QPushButton { background-color: #40444b; color: #72767d; border-radius: 8px; padding: 5px 20px; font-weight: bold; border: none;}"
            );
    }
}

void MainWindow::onContactListPressed(const QModelIndex &index)
{
    QListWidgetItem *item = ui->contactList->item(index.row());
    if(!item){
        return;
    }

    QPoint pos = ui->contactList->viewport()->mapFromGlobal(QCursor::pos());
    QRect itemRect = ui->contactList->visualItemRect(item);

    // 添加好友按钮区域
    int btnWidth = 80, btnHeight = 28, margin = 10;
    QRect btnRect(itemRect.right() - btnWidth - margin,itemRect.bottom() - btnHeight - margin,btnWidth,btnHeight);

    // 鼠标点击在按钮内
    if(btnRect.contains(pos)){
        int targetId = item->data(ContactDelegate::RoleStatus).toInt();
        bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();

        if(!isFriend && targetId != m_currentUserId){
            AddFriendReq req;
            req.targetId = targetId;
            NetworkManager::instance().sendMsg(MSG_ADD_FRIEND_REQ,QByteArray((char*)&req,sizeof(AddFriendReq)));
            QMessageBox::information(this,"提示","好友请求已发送");
        }
        return;
    }
    onContactListClicked(item);
}

void MainWindow::onSigMsgReceived(uint32_t srcId, QByteArray body)
{
    if (body.isEmpty()) return;

    char subType = body[0];
    QByteArray realData = body.mid(1);

    if (subType == SUB_TEXT) {
        QString text = QString::fromUtf8(realData);

        ChatMessage msg(text, false, ":/res/you.jpeg"); // 对方头像
        if (m_currentFriendId == srcId) {
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
        } else {
            // 否则应该显示红点提示
            m_chatHistory[srcId].append(msg);
            // 2. 【核心】找到好友列表里对应的 Item，增加未读计数
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int uid = item->data(Qt::UserRole).toInt();
                if (uid == srcId) {
                    // 取出当前未读数 (UserRole + 2)
                    int currentCount = item->data(Qt::UserRole + 2).toInt();
                    // 加 1
                    item->setData(Qt::UserRole + 2, currentCount + 1);
                    break;
                }
            }
        }
        // 同时保存到本地历史记录
        if (!m_chatHistory[m_currentFriendId].isEmpty() || m_currentFriendId == srcId) {
            m_chatHistory[srcId].append(msg);
        }

    }else if (subType == SUB_IMAGE) {
        QByteArray realImageData = body.mid(1);
        ChatMessage msg(realImageData, false, ":/res/you.jpeg");
        if (m_currentFriendId == srcId) {
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
        }else{
            // 红点显示
            m_chatHistory[srcId].append(msg);
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int uid = item->data(Qt::UserRole).toInt();
                if (uid == srcId) {
                    // 取出当前未读数 (UserRole + 2)
                    int currentCount = item->data(Qt::UserRole + 2).toInt();
                    // 加 1
                    item->setData(Qt::UserRole + 2, currentCount + 1);
                    break;
                }
            }
        }
        // 同时保存到本地历史记录
        if (!m_chatHistory[m_currentFriendId].isEmpty() || m_currentFriendId == srcId) {
            m_chatHistory[srcId].append(msg);
        }
    }
}

void MainWindow::onSigChatHistoryReceived(int friendId, const QList<QPair<int, QByteArray> > &history)
{
    if (friendId != m_currentFriendId) return;

    for (const auto &p : history) {
        bool isMe = (p.first == m_currentUserId);
        QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";

        QByteArray rawBody = p.second;
        if (rawBody.isEmpty()) continue;

        // 1. 获取协议头 (第一个字节)
        char msgType = rawBody[0];

        // 2. 获取实际内容 (去掉第一个字节)
        QByteArray realContent = rawBody.mid(1);

        if (msgType == SUB_IMAGE) {
            // --- 图片处理 ---
            // 调用接收 QByteArray 的构造函数，这会将 type 设置为 TypeImage
            ChatMessage msg(realContent, isMe, avatar);
            m_chatModel->addMessage(msg);

        } else {
            // --- 文本处理 ---
            // 调用接收 QString 的构造函数，这会将 type 设置为 TypeText
            QString text = QString::fromUtf8(realContent);
            ChatMessage msg(text, isMe, avatar);
            m_chatModel->addMessage(msg);
        }
    }
    ui->chatList->scrollToBottom();
}

void MainWindow::onSigFriendStatusChanged(int uid, int status)
{
    // 遍历列表控件的所有项
    for (int i = 0; i < ui->contactList->count(); ++i) {
        QListWidgetItem* item = ui->contactList->item(i);

        // 我们之前把 UID 存在 UserRole 里了，现在取出来对比
        int itemUid = item->data(Qt::UserRole).toInt();

        if (itemUid == uid) {
            // 更新在线状态
            QString name = item->data(Qt::UserRole + 1).toString();

            if (status == 1) {
                item->setText(QString("[在线] %1").arg(name));
                item->setForeground(QBrush(Qt::green)); // 在线变绿
            } else {
                item->setText(QString("[离线] %1").arg(name));
                item->setForeground(QBrush(Qt::gray));  // 离线变灰
            }

            // 重新排序 (可选：让在线的排到最上面)
            // ui->contactList->sortItems();
            break;
        }
    }
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    // 每次输入都重置定时器，防止频繁请求
    m_searchTimer->start();
}

void MainWindow::onSearchTimerTimeout()
{
    QString keyword = ui->searchEdit->text().trimmed();

    if(keyword.isEmpty()){
        // 搜索框清空后再次显示回好友列表
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ,QByteArray());
    }else{
        // 发送搜索请求
        SearchReq req;
        memset(&req,0,sizeof(SearchReq));
        strncpy(req.keyword,keyword.toStdString().c_str(),32);

        NetworkManager::instance().sendMsg(MSG_SEARCH_USER_REQ,QByteArray((char*)&req,sizeof(SearchReq)));
    }
}

void MainWindow::onSigSearchUserResult(QList<FriendInfo> list)
{
    ui->contactList->clear();

    for(const auto &info : list){
        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(200,60));

        // 判断是否是当前用户好友（排除自己）
        bool isFriend = m_friendIds.contains(info.id) || info.id == m_currentUserId;

        // item中保存用户id，用户名，是否与当前用户是好友关系
        item->setData(ContactDelegate::RoleStatus,info.id);
        item->setData(ContactDelegate::RoleName,QString::fromUtf8(info.userName));
        item->setData(ContactDelegate::RoleIsFriend,isFriend);

        QString status = (info.status == 1)? "[在线]" : "[离线]";
        item->setText(QString("%1 %2").arg(status,QString::fromUtf8(info.userName)));

        ui->contactList->addItem(item);
    }
}

void MainWindow::onSigFriendRequestReceived(int uid, const QString name)
{

    // 防止重复添加
    if (std::any_of(m_pendingRequests.cbegin(), m_pendingRequests.cend(),
                    [uid](const FriendRequest& r) { return r.requesterId == uid; })) {
        return;
    }

    m_pendingRequests.append({uid,name});

    // 如果当前在新朋友界面则刷新
    if(ui->stackedWidget->currentIndex() == 1){
        updateNewFriendsPage();
    }
}

void MainWindow::onSigFriendRequestAccepted()
{
    NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
}

void MainWindow::onSigFriendRequestRejected()
{
    QMessageBox::warning(this, "好友请求被拒",
                         "对方拒绝了你的好友请求", QMessageBox::Ok);
}

void MainWindow::updateNewFriendsPage()
{
    ui->friendRequestsList->clear();
    // 设置列表为按行选择，防止选中时样式怪异
    ui->friendRequestsList->setSelectionMode(QAbstractItemView::NoSelection);

    for (const auto &req : m_pendingRequests) {
        // 1. 创建 ListWidgetItem
        QListWidgetItem *item = new QListWidgetItem(ui->friendRequestsList);
        // 设置合适的高度，60-70px 通常比较美观
        item->setSizeHint(QSize(0, 70));
        // 注意：不要在这里 item->setText()，因为会被 setItemWidget 挡住
        item->setData(Qt::UserRole, req.requesterId);

        // 2. 创建容器 Widget
        QWidget *widget = new QWidget;
        // 设置背景透明，否则可能会有奇怪的灰色背景块
        widget->setStyleSheet("background-color: transparent;");

        // 3. 创建水平布局
        QHBoxLayout *layout = new QHBoxLayout(widget);
        // 【关键】设置边距，防止内容被裁剪。参数：左，上，右，下
        layout->setContentsMargins(15, 5, 15, 5);
        layout->setSpacing(10); // 控件之间的间距

        // 4. 创建名字 Label (替代原来的 item->setText)
        QLabel *nameLabel = new QLabel(req.requesterName);
        nameLabel->setStyleSheet("color: #dcddde; font-size: 12px; font-weight: bold;");

        // 5. 创建按钮
        QPushButton *accept = new QPushButton("同意");
        QPushButton *reject = new QPushButton("拒绝");

        // 设置按钮大小
        accept->setFixedSize(40, 25);
        reject->setFixedSize(40, 25);

        // 设置按钮样式 (保持你原来的风格，微调对齐)
        accept->setStyleSheet(
            "QPushButton { background: #07c160; color: white; border-radius: 4px; border: none; font-size: 10px; }"
            "QPushButton:hover { background: #06ad56; }"
            "QPushButton:pressed { background: #059b4d; }"
            );
        reject->setStyleSheet(
            "QPushButton { background: #ff4d4f; color: white; border-radius: 4px; border: none; font-size: 10px; }"
            "QPushButton:hover { background: #e63f41; }"
            "QPushButton:pressed { background: #d9363e; }"
            );

        // 6. 处理按钮点击事件 (使用 Lambda 捕获 id)
        connect(accept, &QPushButton::clicked, this, [=](){
            sendFriendResponse(req.requesterId,true);
            removeRequestAndRefresh(req.requesterId);
        });

        connect(reject, &QPushButton::clicked, this, [=](){
            sendFriendResponse(req.requesterId,false);
            removeRequestAndRefresh(req.requesterId);
        });

        // 7. 添加到布局： 名字 -> 弹簧 -> 同意 -> 拒绝
        layout->addWidget(nameLabel);
        layout->addStretch(); // 弹簧，把按钮顶到最右边
        layout->addWidget(accept);
        layout->addWidget(reject);

        // 8. 将 Widget 设置给 Item
        ui->friendRequestsList->setItemWidget(item, widget);
    }
}

void MainWindow::on_btnSend_clicked()
{
    QString text = ui->msgEdit->toPlainText();
    if(text.isEmpty() || m_currentFriendId == 0){
        return;
    }

    // 打包信息发送给服务器
    QByteArray body;
    body.append((char)SUB_TEXT); // 必须加上 SUB_TEXT (通常定义为 1 或 0)
    body.append(text.toUtf8());

    // 打包信息发送给服务器 (注意：这里传 body 而不是 text.toUtf8())
    QByteArray packet = makePacket(MSG_CHAT_TEXT, body, 0, m_currentFriendId);
    NetworkManager::instance().sendRow(packet);

    // true=我
    ChatMessage msg(text,true,":/res/me.jpg");
    m_chatModel->addMessage(msg);
    ui->chatList->scrollToBottom();

    ui->msgEdit->clear();
}

void MainWindow::setCurrentUserId(int newCurrentUserId)
{
    m_currentUserId = newCurrentUserId;
}


void MainWindow::on_btnContact_clicked()
{
    if(ui->stackedWidget->currentIndex() != 0){
        ui->stackedWidget->setCurrentIndex(0);
        ui->searchEdit->clear();
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
    }
}


void MainWindow::on_btnNewFriends_clicked()
{
    if(ui->stackedWidget->currentIndex() != 1){
        ui->stackedWidget->setCurrentIndex(1);
        updateNewFriendsPage();
    }
}


void MainWindow::on_btnImage_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择图片", "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)"
        );
    if (filePath.isEmpty() || m_currentFriendId == 0) return;

    QImage image(filePath);
    if (image.isNull()) {
        QMessageBox::warning(this, "错误", "无法加载图片");
        return;
    }

    // 限制大小（可选（比如压缩到 800px 宽）
    if (image.width() > 800) {
        image = image.scaledToWidth(800, Qt::SmoothTransformation);
    }

    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 85); // 压缩质量 85

    // 构造带类型的 body：第一个字节是 SUB_IMAGE，其余是图片数据
    QByteArray body;
    body.append((char)SUB_IMAGE);
    body.append(imageData);

    // 发送！注意：这里用 sendRow，因为我们自己打包了 header
    QByteArray packet = makePacket(MSG_CHAT_TEXT, body, 0, m_currentFriendId);
    NetworkManager::instance().sendRow(packet);

    // 本地立即显示
    ChatMessage msg(imageData, true, ":/res/me.jpg");
    m_chatModel->addMessage(msg);
    ui->chatList->scrollToBottom();
}

