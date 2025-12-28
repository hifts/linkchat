#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "networkmanager.h"
#include "filetransfermanager.h"
#include "filereceiver.h"
#include "groupdialog.h"
#include "logger.h"
#include "encryptionmanager.h"

#include <QBuffer>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QButtonGroup>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
{
    // 先连接NetworkManager的关键信号，确保不会错过服务器推送的数据
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestReceived,this,&MainWindow::onSigFriendRequestReceived);
    connect(&NetworkManager::instance(),&NetworkManager::sigMsgReceived,this,&MainWindow::onSigMsgReceived);
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendListReceived,this,&MainWindow::onFriendListReceived);
    connect(&NetworkManager::instance(),&NetworkManager::sigChatHistoryReceived,this,&MainWindow::onSigChatHistoryReceived);
    connect(&NetworkManager::instance(), &NetworkManager::sigFriendStatusChanged,this,&MainWindow::onSigFriendStatusChanged);
    connect(&NetworkManager::instance(),&NetworkManager::sigSearchUserResult,this,&MainWindow::onSigSearchUserResult);
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestAccepted,this,&MainWindow::onSigFriendRequestAccepted);
    connect(&NetworkManager::instance(),&NetworkManager::sigFriendRequestRejected,this,&MainWindow::onSigFriendRequestRejected);
    
    ui->setupUi(this);

    setMouseTracking(true);
    
    // 安装事件过滤器到主窗口，用于处理子控件的鼠标事件
    installEventFilter(this);
    
    // 给所有子控件安装事件过滤器并启用鼠标追踪
    QList<QWidget*> allWidgets = findChildren<QWidget*>();
    for (QWidget *widget : allWidgets) {
        widget->installEventFilter(this);
        widget->setMouseTracking(true);
    }

    //初始化界面
    initUI();

    initModel();
    
    // 在UI初始化后，再次给所有子控件安装事件过滤器并启用鼠标追踪
    allWidgets = findChildren<QWidget*>();
    for (QWidget *widget : allWidgets) {
        widget->installEventFilter(this);
        widget->setMouseTracking(true);
    }

    m_searchTimer = new QTimer(this);
    m_searchTimer->setInterval(300);        // 隔300ms再触发搜索
    m_searchTimer->setSingleShot(true);     // 只触发一次

    // 连接剩余的UI相关信号
    connectSignalsAndSlots();

    // 初始化新朋友按钮状态
    updateNewFriendsButtonState();

    // 界面出来后刷新好友列表和群列表(向服务器请求信息)
    // 使用 QTimer::singleShot 0ms 延时，确保构造函数执行完后再发包
    QTimer::singleShot(0,[](){
        // 发送空包即可，因为 Server 知道你是谁
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ,QByteArray());
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ,QByteArray());
    });

    // 延迟检查未完成的传输任务
    QTimer::singleShot(1000, this, &MainWindow::checkIncompleteTransfers);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 清除密钥缓存
    EncryptionManager::instance().clearKeyCache();
    
    // 主动断开连接（这也会清除密钥缓存，但为了安全起见再次调用）
    if (NetworkManager::instance().isConnected()) {
        NetworkManager::instance().disconnectFromServer();
    }
    
    LOG_INFO("[MainWindow] Sensitive data cleared");
    
    // 接受关闭事件
    event->accept();
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

    //  如果正在调整大小 → 直接 resize
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
    // 只有在没有按下按钮时才更新光标
    if (!(event->buttons() & Qt::LeftButton)) {
        updateCursorShape(pos);
    }

    // 只有按住左键且不是调整窗口大小才处理窗口拖动
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

void MainWindow::leaveEvent(QEvent *event)
{
    // 鼠标离开窗口时，如果不在调整大小，恢复箭头光标
    if (m_resizeDir == None) {
        setCursor(Qt::ArrowCursor);
    }
    QWidget::leaveEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 当鼠标进入任何子控件时，如果不在调整大小状态，恢复箭头光标
    if (event->type() == QEvent::Enter && m_resizeDir == None) {
        setCursor(Qt::ArrowCursor);
    }
    
    // 当鼠标在子控件上移动时，检查是否在窗口边缘
    if (event->type() == QEvent::MouseMove && m_resizeDir == None) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QWidget *widget = qobject_cast<QWidget*>(watched);
        if (widget) {
            QPoint globalPos = widget->mapToGlobal(mouseEvent->pos());
            QPoint windowPos = mapFromGlobal(globalPos);
            
            // 只有在边缘区域才更新光标，否则保持箭头
            ResizeDirection dir = getResizeDirection(windowPos);
            if (dir == None) {
                setCursor(Qt::ArrowCursor);
            } else {
                updateCursorShape(windowPos);
            }
        }
    }
    
    return QWidget::eventFilter(watched, event);
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
    // 无边框 + 拖拽
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    ui->stackedWidget->setCurrentIndex(0);

    // 创建按钮组实现互斥选中效果
    QButtonGroup *navButtonGroup = new QButtonGroup(this);
    navButtonGroup->setExclusive(true);  // 设置互斥，同一时间只能选中一个
    navButtonGroup->addButton(ui->btnAvatar);
    navButtonGroup->addButton(ui->btnChat);
    navButtonGroup->addButton(ui->btnContact);
    navButtonGroup->addButton(ui->btnNewFriends);

    // 默认选中"会话"按钮，并隐藏聊天内容
    ui->btnChat->setChecked(true);
    ui->lblChatTitle->setVisible(false);
    ui->chatList->setVisible(false);
    ui->msgEdit->setVisible(false);
    ui->btnSend->setVisible(false);
    ui->btnImage->setVisible(false);
    ui->btnFile->setVisible(false);



    // 在 MainWindow 构造函数里，initUI() 之后加上这句：
    ui->chatList->setMouseTracking(true);
    ui->chatList->viewport()->setAttribute(Qt::WA_Hover);

    // UI 样式
    QString style = R"(
    /* 全局设置：字体与去边框 */
    QWidget {
        font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
        font-size: 14px;
        border: none;
    }

    /* 左侧侧边栏：最深色 (#202225) */
    QWidget#sideBar {
        background-color: #202225;
        border: none; /* 确保自身无边框 */
    }

    /* 侧边栏通用按钮 */
    QWidget#sideBar QPushButton {
        background-color: transparent;
        color: #b9bbbe;       /* 浅灰色文字 */
        border: none;         /* 去掉按钮边框 */
        outline: none;
        border-radius: 10px;
        padding: 10px 5px;    /* 上下10px，左右缩减为5px，给文字留空 */
        margin: 5px 8px;      /* 按钮左右留点空隙，不要贴边 */
        font-weight: bold;
        font-size: 10px;
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

    /* 中间列表区：中灰色 (#2f3136) */
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

    /* 加号按钮样式 */
    QPushButton#btnGroup {
        background-color: #202225; /* 与搜索框同色或稍浅 */
        color: #b9bbbe;
        border-radius: 4px;
        font-size: 20px;
        font-weight: bold;
        margin: 10px 10px 0px 5px; /* 上 右 下 左 */
    }
    QPushButton#btnGroup:hover {
        background-color: #5865F2; /* 悬停变蓝 */
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

    /* 右侧聊天区：最亮色 (#36393f) */
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

    /* 好友列表滚动条 */
    QListWidget#contactList QScrollBar:vertical {
        background-color: transparent;
        width: 8px;
        margin: 4px 2px 4px 0px;
        border-radius: 4px;
    }
    QListWidget#contactList QScrollBar::handle:vertical {
        background-color: #202225;
        min-height: 30px;
        border-radius: 4px;
    }
    QListWidget#contactList QScrollBar::handle:vertical:hover {
        background-color: #4f545c;
    }
    QListWidget#contactList QScrollBar::handle:vertical:pressed {
        background-color: #5865F2;
    }
    QListWidget#contactList QScrollBar::add-line:vertical,
    QListWidget#contactList QScrollBar::sub-line:vertical {
        height: 0px;
        background: none;
    }
    QListWidget#contactList QScrollBar::add-page:vertical,
    QListWidget#contactList QScrollBar::sub-page:vertical {
        background: none;
    }

    /* 聊天消息列表滚动条 */
    QListView#chatList QScrollBar:vertical {
        background-color: transparent;
        width: 8px;
        margin: 4px 2px 4px 0px;
        border-radius: 4px;
    }
    QListView#chatList QScrollBar::handle:vertical {
        background-color: #202225;
        min-height: 30px;
        border-radius: 4px;
    }
    QListView#chatList QScrollBar::handle:vertical:hover {
        background-color: #4f545c;
    }
    QListView#chatList QScrollBar::handle:vertical:pressed {
        background-color: #5865F2;
    }
    QListView#chatList QScrollBar::add-line:vertical,
    QListView#chatList QScrollBar::sub-line:vertical {
        height: 0px;
        background: none;
    }
    QListView#chatList QScrollBar::add-page:vertical,
    QListView#chatList QScrollBar::sub-page:vertical {
        background: none;
    }

    /* 新朋友列表滚动条 */
    QListWidget#friendRequestsList QScrollBar:vertical {
        background-color: transparent;
        width: 8px;
        margin: 4px 2px 4px 0px;
        border-radius: 4px;
    }
    QListWidget#friendRequestsList QScrollBar::handle:vertical {
        background-color: #202225;
        min-height: 30px;
        border-radius: 4px;
    }
    QListWidget#friendRequestsList QScrollBar::handle:vertical:hover {
        background-color: #4f545c;
    }
    QListWidget#friendRequestsList QScrollBar::handle:vertical:pressed {
        background-color: #5865F2;
    }
    QListWidget#friendRequestsList QScrollBar::add-line:vertical,
    QListWidget#friendRequestsList QScrollBar::sub-line:vertical {
        height: 0px;
        background: none;
    }
    QListWidget#friendRequestsList QScrollBar::add-page:vertical,
    QListWidget#friendRequestsList QScrollBar::sub-page:vertical {
        background: none;
    }

    /* 底部输入容器 */
    QWidget#inputWidget {
        background-color: #36393f; /* 与背景同色 */
        padding: 10px;
    }

    /* 输入框 QTextEdit */
    QTextEdit#msgEdit {
        background-color: #40444b; /* 输入框背景深灰 */
        color: white;
        border: none;
        border-radius: 8px;
        padding: 10px;
        font-size: 14px;
    }

    /* 发送按钮 */
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

    QPushButton#btnFile {
        color: #72767d;
        border-radius: 20px;
    }
    QPushButton#btnFile:hover {
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

    // 设置联系人列表的上下文菜单
    ui->contactList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->contactList, &QListWidget::customContextMenuRequested, this, &MainWindow::onContactListContextMenu);

}

void MainWindow::connectSignalsAndSlots()
{
    // NetworkManager的关键信号已经在构造函数开始时提前连接
    // 这里只连接UI相关的信号和其他信号
    
    // 计时器
    connect(m_searchTimer,&QTimer::timeout,this,&MainWindow::onSearchTimerTimeout);

    // 选择好友列表聊天点击信号
    connect(ui->contactList,&QListWidget::pressed,this,&MainWindow::onContactListPressed);

    // 监听搜索框
    connect(ui->searchEdit,&QLineEdit::textChanged,this,&::MainWindow::onSearchTextChanged);

    // 关联删除好友响应信号
    connect(&NetworkManager::instance(),&NetworkManager::sigDeleteFriendResponse,this,&MainWindow::onSigDeleteFriendResponse);

    // 关联退出群聊响应信号
    connect(&NetworkManager::instance(),&NetworkManager::sigLeaveGroupResponse,this,&MainWindow::onSigLeaveGroupResponse);

    // 关联文件请求信号
    connect(&NetworkManager::instance(), &NetworkManager::sigFileTransferRequest,this, &MainWindow::onSigFileTransferRequest);

    // 关联是否接收文件信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFileTransferResponse,this,&MainWindow::onsigFileTransferResponse);

    // 关联文件传输信号
    connect(&FileTransferManager::instance(),&FileTransferManager::transferStarted,
    this,[this](const QString &fileId,const QString &fileName){
        // 记录当前活动的传输
        ReconnectTransferManager::instance().saveActiveTransfer(fileId, fileName,m_currentFriendId,true);

        onFileTransferStarted(fileId, fileName);
    });


    connect(&FileTransferManager::instance(),&FileTransferManager::transferProgress,this,&MainWindow::onFileTransferProgress);
    connect(&FileTransferManager::instance(),&FileTransferManager::transferCompleted,
    this,[this](const QString &fileId){
        // 移除活动传输记录
        ReconnectTransferManager::instance().removeCompletedTransfer(fileId);
        onFileTransferCompleted(fileId);
    });

    connect(&FileTransferManager::instance(),&FileTransferManager::transferFailed,this,&MainWindow::onFileTransferFailed);
    connect(&FileTransferManager::instance(),&FileTransferManager::sendFileChunk,this,&MainWindow::onSendFileChunk);
    connect(&FileTransferManager::instance(),&FileTransferManager::transferPaused,this,&MainWindow::onFileTransferPaused);

    // 关联文件接收信号
    connect(&NetworkManager::instance(),&NetworkManager::receiveChunk,this,&MainWindow::onFileReceiveChunk);
    connect(&FileReceiver::instance(),&FileReceiver::receiveProgress,this,&MainWindow::onFileReceiveProgress);

    connect(&FileReceiver::instance(),&FileReceiver::receiveCompleted,
    this,[this](const QString &fileId,const QString &savePath){
        // 移除活动传输记录
        ReconnectTransferManager::instance().removeCompletedTransfer(fileId);
        onFileReceiveCompleted(fileId,savePath);
    });

    connect(&FileReceiver::instance(),&FileReceiver::receiveFailed,this,&MainWindow::onFileReceiveFailed);

    // 关联断线重连传输信号
    connect(&ReconnectTransferManager::instance(),&::ReconnectTransferManager::readyToResumeTransfer,this,&MainWindow::onReadyToResumeTransfers);
    connect(&ReconnectTransferManager::instance(),&ReconnectTransferManager::requestResumeTransfer,this,&MainWindow::onRequestResumeTransfer);

    // 关联断点续传协议信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFileResumeReq,this,&MainWindow::onFileResumeReq);
    connect(&NetworkManager::instance(),&NetworkManager::sigFileResumeResp,this,&MainWindow::onFileResumeResp);

    // 关联群聊信号
    connect(&NetworkManager::instance(), &NetworkManager::sigGroupListReceived, this, &MainWindow::onGroupListReceived);
    connect(&NetworkManager::instance(), &NetworkManager::sigGroupMsgReceived, this, &MainWindow::onGroupMsgReceived);
    connect(&NetworkManager::instance(), &NetworkManager::sigGroupChatHistoryReceived, this, &MainWindow::onGroupChatHistoryReceived);
    connect(&NetworkManager::instance(), &NetworkManager::sigCreateGroupResult, this, &MainWindow::onCreateGroupResult);
    connect(&NetworkManager::instance(), &NetworkManager::sigInviteToGroupNotify, this, &MainWindow::onInviteToGroupNotify);
    
    // 关联加密错误信号
    connect(&EncryptionManager::instance(), &EncryptionManager::keyGenerationError,
            this, [this](int errorType, const QString& errorMessage) {
                QString typeStr = (errorType == 1) ? "私聊密钥" : "群聊密钥";
                QMessageBox::warning(this, "加密错误", 
                    QString("%1生成失败：%2").arg(typeStr, errorMessage));
                LOG_ERROR(QString("[MainWindow] Key generation error (type=%1): %2")
                         .arg(errorType).arg(errorMessage));
            });
    
    connect(&EncryptionManager::instance(), &EncryptionManager::encryptionOperationError,
            this, [this](const QString& errorMessage) {
                QMessageBox::warning(this, "加密错误", 
                    QString("消息加密失败：%1").arg(errorMessage));
                LOG_ERROR(QString("[MainWindow] Encryption operation error: %1").arg(errorMessage));
            });
    
    connect(&EncryptionManager::instance(), &EncryptionManager::decryptionOperationError,
            this, [this](const QString& errorMessage) {
                QMessageBox::warning(this, "解密错误", 
                    QString("消息解密失败：%1").arg(errorMessage));
                LOG_ERROR(QString("[MainWindow] Decryption operation error: %1").arg(errorMessage));
            });
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
    
    // 检查是否还有未处理的好友请求，更新红点状态
    m_hasUnreadFriendRequests = !m_pendingRequests.isEmpty();
    updateNewFriendsButtonState();
    
    updateNewFriendsPage();
}

void MainWindow::sendFileTransferRequestForResume(const QString &fileId, const TransferState &state, int friendId)
{
    // 构建文件传输请求
    FileTransferReq req;
    memset(&req, 0, sizeof(FileTransferReq));

    strncpy(req.fileName, state.fileName.toUtf8().constData(), sizeof(req.fileName) - 1);
    req.fileName[255] = '\0';
    req.fileSize = state.fileSize;

    quint64 chunkSize = 64 * 1024;
    req.totalChunks = (state.fileSize + chunkSize - 1) / chunkSize;

    strncpy(req.fileId, fileId.toUtf8().constData(), sizeof(req.fileId) - 1);
    req.fileId[63] = '\0';

    // 发送请求
    QByteArray packet = makePacket(MSG_FILE_TRANSFER_REQ, QByteArray((char*)&req, sizeof(FileTransferReq)),0,friendId);
    NetworkManager::instance().sendRow(packet);

    LOG_INFO(QString("重新发送文件传输请求: %1 (已完成 %2/%3 分片)").arg(state.fileName).arg(state.completedChunks.size()).arg(state.totalChunks));

    // 待处理列表添加
    m_pendingFileTransfers[fileId] = state.filePath;
}

void MainWindow::checkIncompleteTransfers()
{
    // 获取所有未完成的传输
    QList<TransferState> incompleteTransfers = TransferStateManager::instance().getIncompleteTransfers();

    if(incompleteTransfers.isEmpty()){
        return;
    }

    LOG_INFO_FMT("发现 %1 个未完成的传输任务", incompleteTransfers.size());

    // 构建提示消息
    QString msg = QString("发现 %1 个未完成的文件传输：\n").arg(incompleteTransfers.size());

    int displayCount = qMin(5, incompleteTransfers.size());
    for(int i = 0; i < displayCount; i++){
        const TransferState &state = incompleteTransfers[i];
        QString status = state.isSending ? "发送中" : "接收中";
        int progress = state.totalChunks > 0 ?
            (state.completedChunks.size() * 100) / state.totalChunks : 0;
        msg += QString("\n• %1 (%2, %3%)")
                   .arg(state.fileName, status)
                   .arg(progress);
    }

    if(incompleteTransfers.size() > 5){
        msg += QString("\n... 共 %1 个").arg(incompleteTransfers.size());
    }

    msg += "\n\n是否尝试恢复这些传输？";

    QMessageBox::StandardButton res = QMessageBox::question(
        this, "恢复未完成的传输", msg,
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Ignore);

    if(res == QMessageBox::Yes){
        // 尝试恢复所有传输
        for(const TransferState &state : incompleteTransfers){
            if(state.isSending){
                // 发送端：检查文件是否存在
                if(QFile::exists(state.filePath)){
                    // 记录到活动传输
                    ReconnectTransferManager::instance().saveActiveTransfer(
                        state.fileId, state.fileName, state.friendId, true);

                    // 发送恢复请求
                    NetworkManager::instance().requestResumeTransfer(state.fileId, state.friendId);

                    QString displayText = QString("[请求恢复] %1").arg(state.fileName);
                    ChatMessage chatMsg(displayText, true, ":/res/me.jpg");
                    m_chatModel->addMessage(chatMsg);
                } else {
                    LOG_WARN_FMT("文件不存在，无法恢复: %1", state.filePath);
                    TransferStateManager::instance().removeTransferState(state.fileId);
                }
            } else {
                // 接收端：等待对方继续发送
                ReconnectTransferManager::instance().saveActiveTransfer(
                    state.fileId, state.fileName, state.friendId, false);

                QString displayText = QString("[等待恢复] %1 (%2%)")
                                          .arg(state.fileName)
                                          .arg(state.totalChunks > 0 ?
                                                   (state.completedChunks.size() * 100) / state.totalChunks : 0);
                ChatMessage chatMsg(displayText, false, ":/res/you.jpeg");
                m_chatModel->addMessage(chatMsg);
            }
        }
        ui->chatList->scrollToBottom();
    } else if(res == QMessageBox::Ignore){
        // 清理所有未完成的传输状态
        for(const TransferState &state : incompleteTransfers){
            TransferStateManager::instance().removeTransferState(state.fileId);

            // 如果是接收端，删除临时文件
            if(!state.isSending && !state.tempFilePath.isEmpty()){
                QFile::remove(state.tempFilePath);
            }
        }
        LOG_INFO("用户选择忽略并清理未完成的传输");
    }
    // 如果选择No，保留状态但不恢复
}

void MainWindow::updateContactLastMsgTime(int friendId, const QDateTime &time)
{
    // 更新时间记录
    if (friendId > 0) {
        m_lastMsgTime[friendId] = time;
    } else if (friendId < 0) {
        // 群聊（负数ID）
        m_groupLastMsgTime[-friendId] = time;
    }
    
    // 更新好友列表中对应项的显示
    for (int i = 0; i < ui->contactList->count(); ++i) {
        QListWidgetItem *item = ui->contactList->item(i);
        int itemId = item->data(ContactDelegate::RoleStatus).toInt();
        
        if (itemId == friendId) {
            item->setData(ContactDelegate::RoleLastMsgTime, time);
            // 触发重绘 - 使用 viewport()->update()
            ui->contactList->viewport()->update();
            break;
        }
    }
}

void MainWindow::updateContactListMode(bool isSessionMode)
{
    // 切换模式时，重新请求好友列表和群聊列表
    // 这样可以根据当前模式过滤显示
    NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
    NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ, QByteArray());
}

void MainWindow::updateNewFriendsButtonState()
{
    if (m_hasUnreadFriendRequests) {
        // 有未读的好友请求，显示一个简单的点
        ui->btnNewFriends->setText("🔔 新朋友 •");
        ui->btnNewFriends->setStyleSheet("");
    } else {
        // 没有未读的好友请求，隐藏红点
        ui->btnNewFriends->setText("🔔 新朋友");
        ui->btnNewFriends->setStyleSheet("");
    }
}

void MainWindow::onFriendListReceived(QList<FriendInfo> list)
{
    ui->contactList->clear();
    m_friendIds.clear();

    // 判断当前是否是会话模式
    bool isSessionMode = ui->btnChat->isChecked();

    for(const auto &info : list){

        // 保存好友id
        m_friendIds.insert(info.id);
        
        // 会话模式下，只显示有聊天记录的好友
        if (isSessionMode && info.lastMsgTime == 0) {
            continue; // 跳过没有聊天记录的好友
        }

        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(300,60));

        // 在item里存放用户id和用户名
        item->setData(ContactDelegate::RoleStatus,info.id);
        item->setData(ContactDelegate::RoleName,QString::fromUtf8(info.userName));
        item->setData(ContactDelegate::RoleIsFriend, true);
        item->setData(ContactDelegate::RoleShowTime, isSessionMode); // 设置是否显示时间
        
        // 设置最后消息时间
        if (info.lastMsgTime > 0) {
            QDateTime lastTime = QDateTime::fromSecsSinceEpoch(info.lastMsgTime);
            item->setData(ContactDelegate::RoleLastMsgTime, lastTime);
            m_lastMsgTime[info.id] = lastTime;
        }

        QString status = (info.status == 1)? "[在线]" : "[离线]";
        item->setText(QString("%1 %2").arg(status,QString::fromUtf8(info.userName)));

        // 设置头像
        // item->setIcon();

        ui->contactList->addItem(item);
    }
}

void MainWindow::onContactListClicked(QListWidgetItem *item)
{
    int id = item->data(ContactDelegate::RoleStatus).toInt();
    QString name = item->data(ContactDelegate::RoleName).toString();
    bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();

    // 判断是群聊还是私聊（负数ID表示群聊）
    if (id < 0) {
        // 群聊
        int groupId = -id;
        if (m_isGroupChat && m_currentGroupId == groupId) {
            return; // 已经在当前群聊
        }

        // 切换到群聊模式
        m_isGroupChat = true;
        m_currentGroupId = groupId;
        m_currentFriendId = 0;

        ui->lblChatTitle->setText(name);
        m_chatModel->clearMessages();

        // 请求群聊历史消息
        QByteArray body;
        QDataStream ds(&body, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds << quint32(groupId);
        NetworkManager::instance().sendMsg(MSG_GROUP_CHAT_HISTORY_REQ, body);

        // 清除未读红点
        item->setData(ContactDelegate::RoleUnread, 0);

        // 启用输入
        ui->msgEdit->setEnabled(true);
        ui->btnSend->setEnabled(true);
        ui->btnSend->setStyleSheet(
            "QPushButton { background-color: #5865F2; color: white; border-radius: 8px; padding: 5px 20px; font-weight: bold; }"
            "QPushButton:hover { background-color: #4752c4; }"
        );
        
        // 只有在会话模式下才显示聊天内容
        if(ui->btnChat->isChecked()){
            ui->lblChatTitle->setVisible(true);
            ui->chatList->setVisible(true);
            ui->msgEdit->setVisible(true);
            ui->btnSend->setVisible(true);
            ui->btnImage->setVisible(true);
            ui->btnFile->setVisible(true);
        }
    } else {
        // 私聊
        if (id <= 0 || id == m_currentFriendId) {
            return;
        }

        // 切换到私聊模式
        m_isGroupChat = false;
        m_currentGroupId = 0;
        m_currentFriendId = id;

        ui->lblChatTitle->setText(name);
        m_chatModel->clearMessages();

        QByteArray body;
        QDataStream ds(&body, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds << quint32(id);
        NetworkManager::instance().sendMsg(MSG_CHAT_HISTORY_REQ, body);

        item->setData(ContactDelegate::RoleUnread, 0);
        ui->chatList->scrollToBottom();

        if (isFriend) {
            ui->lblChatTitle->setText(name);
            ui->msgEdit->setEnabled(true);
            ui->msgEdit->setPlaceholderText("");
            ui->btnSend->setEnabled(true);
            ui->btnSend->setStyleSheet(
                "QPushButton { background-color: #5865F2; color: white; border-radius: 8px; padding: 5px 20px; font-weight: bold; }"
                "QPushButton:hover { background-color: #4752c4; }"
            );
            
            // 只有在会话模式下才显示聊天内容
            if(ui->btnChat->isChecked()){
                ui->lblChatTitle->setVisible(true);
                ui->chatList->setVisible(true);
                ui->msgEdit->setVisible(true);
                ui->btnSend->setVisible(true);
                ui->btnImage->setVisible(true);
                ui->btnFile->setVisible(true);
            }
        } else {
            ui->lblChatTitle->setText("选择一个好友开始聊天");
            ui->msgEdit->setEnabled(false);
            ui->btnSend->setEnabled(false);
            ui->btnSend->setStyleSheet(
                "QPushButton { background-color: #40444b; color: #72767d; border-radius: 8px; padding: 5px 20px; font-weight: bold; border: none;}"
            );
        }
    }
}

void MainWindow::onContactListPressed(const QModelIndex &index)
{
    qDebug() << "[MainWindow] Contact list pressed, index:" << index.row();
    
    QListWidgetItem *item = ui->contactList->item(index.row());
    if(!item){
        qDebug() << "[MainWindow] No item found at index";
        return;
    }

    // 获取点击位置 - 使用正确的坐标系
    QPoint globalPos = QCursor::pos();
    QPoint localPos = ui->contactList->mapFromGlobal(globalPos);
    QRect itemRect = ui->contactList->visualItemRect(item);
    
    qDebug() << "[MainWindow] Global pos:" << globalPos;
    qDebug() << "[MainWindow] Local pos:" << localPos;
    qDebug() << "[MainWindow] Item rect:" << itemRect;

    // 添加好友按钮区域 - 与ContactDelegate中的绘制区域保持一致，但扩大点击区域
    int btnWidth = 65, btnHeight = 24, margin = 8;
    
    // 扩大点击区域，让用户更容易点击到
    int expandedWidth = btnWidth + 20;  // 左右各扩大10px
    int expandedHeight = btnHeight + 16; // 上下各扩大8px
    
    // 按钮位置：使用绝对坐标（相对于contactList）
    QRect btnRect(
        itemRect.right() - expandedWidth - margin + 10,  // 调整x位置以保持居中
        itemRect.top() + (itemRect.height() - expandedHeight) / 2,   // 垂直居中
        expandedWidth,
        expandedHeight
    );
    
    qDebug() << "[MainWindow] Button rect (absolute):" << btnRect;
    qDebug() << "[MainWindow] Is click in button?" << btnRect.contains(localPos);

    // 检查是否点击在按钮内
    if(btnRect.contains(localPos)){
        qDebug() << "[MainWindow] Click is within button area";
        
        int targetId = item->data(ContactDelegate::RoleStatus).toInt();
        bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();
        
        qDebug() << "[MainWindow] Target ID:" << targetId << "Is friend:" << isFriend << "Current user ID:" << m_currentUserId;

        if(!isFriend && targetId != m_currentUserId){
            qDebug() << "[MainWindow] Sending friend request to:" << targetId;
            AddFriendReq req;
            req.targetId = targetId;
            NetworkManager::instance().sendMsg(MSG_ADD_FRIEND_REQ,QByteArray((char*)&req,sizeof(AddFriendReq)));
            QMessageBox::information(this,"提示","好友请求已发送");
        } else {
            qDebug() << "[MainWindow] Cannot send friend request - already friend or self";
        }
        return;
    }
    
    qDebug() << "[MainWindow] Click is outside button area, proceeding with normal click";
    onContactListClicked(item);
}

void MainWindow::onSigMsgReceived(uint32_t srcId, QByteArray body)
{
    if (body.isEmpty()) return;

    char subType = body[0];
    QByteArray realData = body.mid(1);

    if (subType == SUB_TEXT) {
        // 检查解密是否失败（内容为空）
        if (realData.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received message with empty content from user %1 - decryption may have failed", srcId);
            QString errorText = "[消息解密失败]";
            ChatMessage msg(errorText, false, ":/res/you.jpeg");
            
            if (m_currentFriendId == srcId) {
                m_chatModel->addMessage(msg);
                ui->chatList->scrollToBottom();
            } else {
                m_chatHistory[srcId].append(msg);
                // 增加未读计数
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int uid = item->data(Qt::UserRole).toInt();
                    if (uid == srcId) {
                        int currentCount = item->data(Qt::UserRole + 2).toInt();
                        item->setData(Qt::UserRole + 2, currentCount + 1);
                        break;
                    }
                }
            }
            return;
        }
        
        QString text = QString::fromUtf8(realData);

        ChatMessage msg(text, false, ":/res/you.jpeg"); // 对方头像
        if (m_currentFriendId == srcId) {
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
        } else {
            // 否则应该显示红点提示
            m_chatHistory[srcId].append(msg);
            // 找到好友列表里对应的 Item，增加未读计数
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
        
        // 更新最后消息时间
        updateContactLastMsgTime(srcId, QDateTime::currentDateTime());

    }else if (subType == SUB_IMAGE) {
        // 检查解密是否失败（内容为空）
        if (realData.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received image with empty content from user %1 - decryption may have failed", srcId);
            QString errorText = "[图片解密失败]";
            ChatMessage msg(errorText, false, ":/res/you.jpeg");
            
            if (m_currentFriendId == srcId) {
                m_chatModel->addMessage(msg);
                ui->chatList->scrollToBottom();
            } else {
                m_chatHistory[srcId].append(msg);
                // 增加未读计数
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int uid = item->data(Qt::UserRole).toInt();
                    if (uid == srcId) {
                        int currentCount = item->data(Qt::UserRole + 2).toInt();
                        item->setData(Qt::UserRole + 2, currentCount + 1);
                        break;
                    }
                }
            }
            return;
        }
        
        ChatMessage msg(realData, false, ":/res/you.jpeg");
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
        
        // 更新最后消息时间
        updateContactLastMsgTime(srcId, QDateTime::currentDateTime());
    }
}

void MainWindow::onSigChatHistoryReceived(int friendId, const QList<std::tuple<int, QByteArray, quint64>> &history)
{
    if (friendId != m_currentFriendId) return;

    for (const auto &item : history) {
        int senderId = std::get<0>(item);
        QByteArray rawBody = std::get<1>(item);
        quint64 timestamp = std::get<2>(item);
        
        bool isMe = (senderId == m_currentUserId);
        QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";

        if (rawBody.isEmpty()) continue;

        // 获取协议头 (第一个字节)
        char msgType = rawBody[0];

        // 获取实际内容 (去掉第一个字节)
        QByteArray content = rawBody.mid(1);

        if (msgType == SUB_IMAGE) {
            // --- 图片处理 ---
            ChatMessage msg(content, isMe, avatar, timestamp);
            m_chatModel->addMessage(msg);

        } else {
            // --- 文本处理 ---
            QString text = QString::fromUtf8(content);
            ChatMessage msg(text, isMe, avatar, timestamp);
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
        item->setSizeHint(QSize(300,60));

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
    
    // 标记有未读的好友请求并更新按钮状态
    m_hasUnreadFriendRequests = true;
    updateNewFriendsButtonState();

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
        // 创建 ListWidgetItem
        QListWidgetItem *item = new QListWidgetItem(ui->friendRequestsList);

        item->setSizeHint(QSize(0, 70));
        item->setData(Qt::UserRole, req.requesterId);

        // 创建容器 Widget
        QWidget *widget = new QWidget;
        // 设置背景透明，否则可能会有奇怪的灰色背景块
        widget->setStyleSheet("background-color: transparent;");

        // 创建水平布局
        QHBoxLayout *layout = new QHBoxLayout(widget);
        // 设置边距，防止内容被裁剪。参数：左，上，右，下
        layout->setContentsMargins(15, 5, 15, 5);
        layout->setSpacing(10); // 控件之间的间距

        // 创建名字 Label (替代原来的 item->setText)
        QLabel *nameLabel = new QLabel(req.requesterName);
        nameLabel->setStyleSheet("color: #dcddde; font-size: 12px; font-weight: bold;");

        // 创建按钮
        QPushButton *accept = new QPushButton("同意");
        QPushButton *reject = new QPushButton("拒绝");

        // 设置按钮大小
        accept->setFixedSize(40, 25);
        reject->setFixedSize(40, 25);

        // 设置按钮样式
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

        // 处理按钮点击事件 (使用 Lambda 捕获 id)
        connect(accept, &QPushButton::clicked, this, [=](){
            sendFriendResponse(req.requesterId,true);
            removeRequestAndRefresh(req.requesterId);
        });

        connect(reject, &QPushButton::clicked, this, [=](){
            sendFriendResponse(req.requesterId,false);
            removeRequestAndRefresh(req.requesterId);
        });

        //  添加到布局： 名字 -> 弹簧 -> 同意 -> 拒绝
        layout->addWidget(nameLabel);
        layout->addStretch();
        layout->addWidget(accept);
        layout->addWidget(reject);

        // 将 Widget 设置给 Item
        ui->friendRequestsList->setItemWidget(item, widget);
    }
}

void MainWindow::onSigFileTransferRequest(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId)
{
    // 接收文件方

    // 检查是否是断点续传
    TransferState state = TransferStateManager::instance().loadTransferState(fileId);
    bool isResume = (!state.fileId.isEmpty() && !state.isSending && state.fileSize == fileSize);

    // 弹出对话框询问是否接收
    QString sizeStr;
    if(fileSize < 1024){
        sizeStr = QString::number(fileSize) + "B";
    }else if(fileSize < 1024 * 1024){
        sizeStr = QString::number(fileSize / 1024.0,'f',2) + "KB";
    }else{
        sizeStr = QString::number(fileSize / (1024.0 * 1024.0),'f',2) + "MB";
    }

    // 根据是否断点续传显示不同提示
    QString msg;
    if(isResume){
        int completedChunks = state.completedChunks.size();
        int totalChunks = state.totalChunks;
        msg = QString("用户 %1 请求继续发送文件：\n"
            "文件名：%2\n"
            "文件大小：%3\n"
            "已接收：%4/%5 分片\n\n"
            "是否继续接收文件？")
                  .arg(senderId).arg(fileName, sizeStr)
                  .arg(completedChunks).arg(totalChunks);
    }else{
        msg = QString("用户 %1 向你发送文件：\n"
            "文件名：%2\n"
            "文件大小：%3\n\n"
            "是否接收文件？")
                  .arg(senderId).arg(fileName, sizeStr);
    }

    QString title = isResume ? "继续接收文件" : "接收文件";
    auto res = QMessageBox::question(this,title,msg,QMessageBox::Yes | QMessageBox::No);

    FileTransferResp resp;
    memset(&resp,0,sizeof(FileTransferResp));
    strncpy(resp.fileId,fileId.toUtf8().constData(),63);
    resp.accepted = (res == QMessageBox::Yes) ? 1 : 0;

    if(res == QMessageBox::Yes){
        // 开始接收文件(支持断点续传，传入发送者ID用于解密)
        FileReceiver::instance().startReceiving(fileId,fileName,fileSize, senderId);

        // 记录活动传输
        ReconnectTransferManager::instance().saveActiveTransfer(fileId, fileName, senderId,false);

        QString displayText;
        if(isResume){
            displayText = QString("[继续接收] %1 (%2)").arg(fileName, sizeStr);
        }else{
            displayText = QString("[接收文件] %1 (%2)").arg(fileName, sizeStr);
        }
        ChatMessage msg(displayText,false,":/res/you.jpeg");
        m_chatModel->addMessage(msg);
        ui->chatList->scrollToBottom();
    }

    QByteArray packet = makePacket(MSG_FILE_TRANSFER_RESP,QByteArray((char*)&resp,sizeof(FileTransferResp)),0,senderId);
    NetworkManager::instance().sendRow(packet);
}

void MainWindow::onsigFileTransferResponse(const QString &fileId, bool accepted)
{
    if (accepted) {
        // 对方同意接收文件

        if(m_pendingFileTransfers.contains(fileId)){
            // 移除并返回value
            QString filePath = m_pendingFileTransfers.take(fileId);

            // 检查是否是断点传续
            TransferState state = TransferStateManager::instance().loadTransferState(fileId);
            if(!state.fileId.isEmpty() && state.completedChunks.size() > 0){
                LOG_INFO_FMT("恢复文件传输 %1（从第 %2 个分片开始）",state.fileName,state.completedChunks.size());
            }

            QString displayText = QString("[恢复传输] %1 (已完成 %2/%3 分片)").arg(state.fileName).arg(state.completedChunks.size()).arg(state.totalChunks);
            ChatMessage msg(displayText,true,":/res/me.jpg");
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
            
            // 开始传输文件
            FileTransferManager::instance().startSendFile(fileId,filePath,m_currentFriendId);
            // QMessageBox::information(this, "成功", "对方已接受文件传输,开始发送...");
        }else{
            LOG_WARN_FMT("File path not found for fileId:%1",fileId);
        }
    } else {
        // 对方拒绝接收文件
        m_pendingFileTransfers.remove(fileId);
        QMessageBox::warning(this, "被拒绝", "对方拒绝接收文件");
    }
}

void MainWindow::onFileTransferStarted(const QString &fileId, const QString &fileName)
{
    // 在聊天界面显示文件传输消息
    QString displayText = QString("[文件] %1").arg(fileName);
    ChatMessage msg(displayText, true, ":/res/me.jpg");
    m_chatModel->addMessage(msg);
    ui->chatList->scrollToBottom();
}

void MainWindow::onFileTransferProgress(const QString &fileId, int percent, qint64 sent, qint64 total)
{
    Q_UNUSED(fileId)
    Q_UNUSED(sent)
    Q_UNUSED(total)

    // 后续完善进度条
    qDebug() << "[UI] Transfer progress:" << percent << "%";
}

void MainWindow::onFileTransferCompleted(const QString &fileId)
{
    Q_UNUSED(fileId)

    QMessageBox::information(this, "文件传输", "文件传输完成！");
    LOG_INFO_FMT("File transfer completed:%1",fileId);
}

void MainWindow::onFileTransferFailed(const QString &fileId, const QString &error)
{
    Q_UNUSED(fileId)

    QMessageBox::warning(this, "传输失败", "文件传输失败: " + error);
    LOG_ERROR_FMT("File %1 transfer failed,%2",fileId,error);
}

void MainWindow::onFileTransferPaused(const QString &fileId, int lastChunkIndex)
{
    LOG_INFO_FMT("File transfer paused:%1, last chunk index:%2",fileId,lastChunkIndex);
    QMessageBox::information(this, "传输暂停", "文件传输已暂停");
}

void MainWindow::onSendFileChunk(const QString &fileId, const QByteArray &chunk, int chunkIndex, int totalChunks, int friendId)
{
    // 构建文件分片包
    FileChunk chunkHeader;
    memset(&chunkHeader,0,sizeof(FileChunk));

    strncpy(chunkHeader.fileId,fileId.toUtf8().constData(),63);
    chunkHeader.chunkIndex = chunkIndex;
    chunkHeader.chunkSize = chunk.size();

    // 组装包体：头部 + 实际数据（已加密）
    QByteArray body;
    body.append((char*)&chunkHeader,sizeof(FileChunk));
    body.append(chunk);

    QByteArray packet = makePacket(MSG_FILE_CHUNK,body,0,friendId);
    NetworkManager::instance().sendRow(packet);

    if (chunkIndex % 10 == 0) {  // 每10个分片打印一次日志
        LOG_INFO_FMT("Send encrypted chunk %1 / %2 for file (size: %3 bytes)",chunkIndex,totalChunks,chunk.size());
    }
}

void MainWindow::onFileReceiveChunk(const QString &fileId, int chunkIndex, const QByteArray &chunk)
{
    FileReceiver::instance().receiveChunk(fileId,chunkIndex,chunk);
}

void MainWindow::onFileReceiveProgress(const QString &fileId, int percent, qint64 received, qint64 total)
{
    // 可以在UI上显示进度条
    qDebug() << "[UI] Receive progress:" << percent << "%"
             << received << "/" << total << "bytes";
}

void MainWindow::onFileReceiveCompleted(const QString &fileId, const QString &savePath)
{
    QMessageBox::information(this, "文件接收完成",
                             QString("文件已保存到:\n%1").arg(savePath));
    LOG_INFO_FMT("File receive completed:%1",savePath);
}

void MainWindow::onFileReceiveFailed(const QString &fileId, const QString &error)
{
    QMessageBox::warning(this, "接收失败", "文件接收失败: " + error);
    LOG_ERROR_FMT("File %1 received failed,%2",fileId,error);
}

void MainWindow::onGroupListReceived(QList<GroupInfo> list)
{
    // 判断当前是否是会话模式
    bool isSessionMode = ui->btnChat->isChecked();
    
    // 在好友列表下方追加群聊列表（用不同样式区分）
    for (const auto &info : list) {
        m_groupIds.insert(info.groupId);
        
        // 群聊在会话模式下也要显示，不管有没有聊天记录
        // 所以不需要过滤

        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(300, 60));

        // 使用负数ID来区分群聊和好友
        item->setData(ContactDelegate::RoleStatus, -info.groupId);  // 负数表示群ID
        item->setData(ContactDelegate::RoleName, QString::fromUtf8(info.groupName));
        item->setData(ContactDelegate::RoleIsFriend, true);  // 群聊也标记为true以允许点击
        item->setData(ContactDelegate::RoleShowTime, isSessionMode); // 设置是否显示时间
        
        // 设置最后消息时间
        if (info.lastMsgTime > 0) {
            QDateTime lastTime = QDateTime::fromSecsSinceEpoch(info.lastMsgTime);
            item->setData(ContactDelegate::RoleLastMsgTime, lastTime);
            m_groupLastMsgTime[info.groupId] = lastTime;
        }

        QString displayText = QString("[群聊] %1 (%2人)").arg(QString::fromUtf8(info.groupName)).arg(info.memberCount);
        item->setText(displayText);

        ui->contactList->addItem(item);
    }
}

void MainWindow::onGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body)
{
    if (body.isEmpty()) return;

    char subType = body[0];
    QByteArray realData = body.mid(1);

    bool isMe = (senderId == m_currentUserId);
    QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";

    if (subType == SUB_TEXT) {
        // 检查解密是否失败（内容为空）
        if (realData.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received group message with empty content from group %1 - decryption may have failed", groupId);
            QString errorText = "[群消息解密失败]";
            ChatMessage msg(errorText, isMe, avatar, senderName);
            
            if (m_isGroupChat && m_currentGroupId == groupId) {
                m_chatModel->addMessage(msg);
                ui->chatList->scrollToBottom();
            } else {
                m_groupChatHistory[groupId].append(msg);
                // 显示未读红点
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int itemId = item->data(ContactDelegate::RoleStatus).toInt();
                    if (itemId < 0 && -itemId == groupId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        break;
                    }
                }
            }
            return;
        }
        
        QString text = QString::fromUtf8(realData);
        ChatMessage msg(text, isMe, avatar, senderName);

        // 如果当前正在这个群聊
        if (m_isGroupChat && m_currentGroupId == groupId) {
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
        } else {
            // 存入历史记录
            m_groupChatHistory[groupId].append(msg);

            // 显示未读红点：找到群聊列表项并增加未读计数
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int itemId = item->data(ContactDelegate::RoleStatus).toInt();

                // 负数ID表示群聊
                if (itemId < 0 && -itemId == groupId) {
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    break;
                }
            }
        }
        
        // 更新最后消息时间（群聊ID为负数）
        updateContactLastMsgTime(-groupId, QDateTime::currentDateTime());
    } else if (subType == SUB_IMAGE) {
        // 检查解密是否失败（内容为空）
        if (realData.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received group image with empty content from group %1 - decryption may have failed", groupId);
            QString errorText = "[群图片解密失败]";
            ChatMessage msg(errorText, isMe, avatar, senderName);
            
            if (m_isGroupChat && m_currentGroupId == groupId) {
                m_chatModel->addMessage(msg);
                ui->chatList->scrollToBottom();
            } else {
                m_groupChatHistory[groupId].append(msg);
                // 显示未读红点
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int itemId = item->data(ContactDelegate::RoleStatus).toInt();
                    if (itemId < 0 && -itemId == groupId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        break;
                    }
                }
            }
            return;
        }
        
        ChatMessage msg(realData, isMe, avatar, senderName);

        if (m_isGroupChat && m_currentGroupId == groupId) {
            m_chatModel->addMessage(msg);
            ui->chatList->scrollToBottom();
        } else {
            m_groupChatHistory[groupId].append(msg);

            // 显示未读红点
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int itemId = item->data(ContactDelegate::RoleStatus).toInt();

                if (itemId < 0 && -itemId == groupId) {
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    break;
                }
            }
        }
        
        // 更新最后消息时间（群聊ID为负数）
        updateContactLastMsgTime(-groupId, QDateTime::currentDateTime());
    }
}

void MainWindow::onGroupChatHistoryReceived(int groupId, const QList<std::tuple<int, QString, QByteArray, quint64>>& history)
{
    if (!m_isGroupChat || groupId != m_currentGroupId) return;

    for (const auto &item : history) {
        int senderId = std::get<0>(item);
        QString senderName = std::get<1>(item);
        QByteArray rawBody = std::get<2>(item);
        quint64 timestamp = std::get<3>(item);

        if (rawBody.isEmpty()) continue;

        bool isMe = (senderId == m_currentUserId);
        QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";

        char msgType = rawBody[0];
        QByteArray realContent = rawBody.mid(1);
        
        // 检查解密是否失败（内容为空）
        if (realContent.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received group history with empty content from group %1 - decryption may have failed", groupId);
            if (msgType == SUB_IMAGE) {
                ChatMessage msg(QString("[历史群图片解密失败]"), isMe, avatar, senderName, timestamp);
                m_chatModel->addMessage(msg);
            } else {
                ChatMessage msg(QString("[历史群消息解密失败]"), isMe, avatar, senderName, timestamp);
                m_chatModel->addMessage(msg);
            }
            continue;
        }

        if (msgType == SUB_IMAGE) {
            ChatMessage msg(realContent, isMe, avatar, senderName, timestamp);
            m_chatModel->addMessage(msg);
        } else {
            QString text = QString::fromUtf8(realContent);
            ChatMessage msg(text, isMe, avatar, senderName, timestamp);
            m_chatModel->addMessage(msg);
        }
    }
    ui->chatList->scrollToBottom();
}

void MainWindow::onCreateGroupResult(bool success, int groupId)
{
    if (success) {
        QMessageBox::information(this, "创建群聊", "群聊创建成功！");

        // 邀请选中的好友加入群
        for (int memberId : m_pendingGroupMembers) {
            InviteToGroupReq req;
            req.groupId = groupId;
            req.targetUserId = memberId;
            NetworkManager::instance().sendMsg(MSG_INVITE_TO_GROUP_REQ, QByteArray((char*)&req, sizeof(InviteToGroupReq)));
        }
        m_pendingGroupMembers.clear();

        // 刷新群列表
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ, QByteArray());
    } else {
        QMessageBox::warning(this, "创建群聊", "群聊创建失败！");
    }
}

void MainWindow::onInviteToGroupNotify(int groupId, const QString &groupName, int inviterId, const QString &inviterName)
{
    QString msg = QString("%1 邀请您加入群聊「%2」").arg(inviterName, groupName);
    QMessageBox::information(this, "群聊邀请", msg);

    // 刷新群列表
    NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ, QByteArray());
}

void MainWindow::onConnectionStateChanged(bool connected)
{
    if(connected){
        LOG_INFO("网络状态已恢复");

        // 通知重连传输管理器
        ReconnectTransferManager::instance().onNetworkReconnected();

        // 延迟请求好友列表和群列表
        QTimer::singleShot(500,[](){
            NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ,QByteArray());
            NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ,QByteArray());
        });
    }else{
        LOG_WARN("网络已断开");
        ReconnectTransferManager::instance().onNetworkDisconnected();
    }
}

void MainWindow::onReconnectStateChanged(int attempts, int delayMs)
{
    QString msg = QString("正在尝试重新连接...(第%1次，等待%2秒)").arg(attempts,delayMs);
    LOG_INFO(msg);
}

void MainWindow::onMaxAttemptsReached()
{
    LOG_INFO("已达到最大重连次数");
    QMessageBox::warning(this,"连接失败","无法连接，请检查网络后重试",QMessageBox::Ok);
}

void MainWindow::onReadyToResumeTransfers(const QList<PendingTransferResume> &transfers)
{
    if(transfers.isEmpty()){
        return;
    }

    LOG_INFO_FMT("准备恢复 %1 个传输任务", transfers.size());

    // 显示提示对话框
    QString msg = QString("检测到 %1 个未完成的传输任务，是否恢复？").arg(transfers.size());

    for(int i = 0; i < qMin(3, transfers.size()); i++){
        const PendingTransferResume &transfer = transfers[i];
        msg += QString("\n%1: %2").arg(transfer.fileName).arg(transfer.isSending ? "发送中" : "接收中");
    }

    if(transfers.size() > 3){
        msg += QString("\n... 共 %1 个").arg(transfers.size());
    }

    QMessageBox::StandardButton res = QMessageBox::question(this, "恢复传输", msg, QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::No) {
        // 用户选择不恢复，清理待恢复任务
        ReconnectTransferManager::instance().clearPendingResumes();
        LOG_INFO("用户取消恢复文件传输");
    }

}

void MainWindow::onRequestResumeTransfer(const QString &fileId, int friendId, bool isSending)
{
    LOG_INFO_FMT("请求恢复传输: %1 (发送中: %2)", fileId, isSending);

    // 加载传输状态
    TransferState state = TransferStateManager::instance().loadTransferState(fileId);

    if(state.fileId.isEmpty()){
        LOG_WARN_FMT("无法加载传输状态: %1", fileId);
        return;
    }

    if(isSending){
        // 恢复发送
        if(!QFile::exists(state.filePath)){
            LOG_WARN_FMT("文件不存在: %1", state.filePath);
            QMessageBox::warning(this, "恢复传输", QString("文件 %1 不存在，无法继续传输").arg(state.fileName));
            TransferStateManager::instance().removeTransferState(fileId);
            return;
        }

        // 先发送恢复传输请求，查询对方已接收的分片
        NetworkManager::instance().requestResumeTransfer(fileId, friendId);

        // 显示提示信息
        QString displayText = QString("[请求恢复] %1").arg(state.fileName);
        ChatMessage msg(displayText, true, ":/res/me.jpg");
        m_chatModel->addMessage(msg);
        ui->chatList->scrollToBottom();
    }else{
        // 恢复接收
        // 接收方只需等待对方继续发送，FileReceiver 会自动处理断点续传
        LOG_INFO_FMT("等待对方继续发送文件: %1", state.fileName);

        // 显示提示信息
        QString displayText = QString("[等待恢复] %1").arg(state.fileName);
        ChatMessage msg(displayText, false, ":/res/you.jpeg");
        m_chatModel->addMessage(msg);
        ui->chatList->scrollToBottom();
    }
}

void MainWindow::onFileResumeReq(const QString &fileId, int senderId)
{
    // 作为接收方，收到发送方的恢复传输请求
    LOG_INFO_FMT("收到恢复传输请求: %1 from %2", fileId, senderId);

    // 检查是否有该文件的接收状态
    ReceivingFileInfo *info = FileReceiver::instance().getReceivingInfo(fileId);
    TransferState state = TransferStateManager::instance().loadTransferState(fileId);

    FileResumeResp resp;
    memset(&resp, 0, sizeof(resp));
    strncpy(resp.fileId, fileId.toUtf8().constData(), 63);

    QByteArray responseBody;

    if(info || (!state.fileId.isEmpty() && !state.isSending)){
        // 可以恢复
        resp.canResume = 1;

        if(info){
            resp.totalChunks = info->totalChunks;
            resp.receivedChunks = info->receivedChunks;
        } else {
            resp.totalChunks = state.totalChunks;
            resp.receivedChunks = state.completedChunks.size();
        }

        responseBody.append((char*)&resp, sizeof(resp));

        // 添加已接收分片位图
        QByteArray bitmap;
        if(info){
            bitmap = FileReceiver::instance().getCompletedChunksBitmap(fileId);
        } else {
            // 从状态构建位图
            int bitmapSize = (state.totalChunks + 7) / 8;
            bitmap = QByteArray(bitmapSize, 0);
            for(int chunkIndex : state.completedChunks){
                int byteIndex = chunkIndex / 8;
                int bitIndex = chunkIndex % 8;
                if(byteIndex < bitmap.size()){
                    bitmap[byteIndex] = bitmap[byteIndex] | (1 << bitIndex);
                }
            }
        }
        responseBody.append(bitmap);

        LOG_INFO_FMT("发送恢复传输响应: canResume=true, received=%1/%2", resp.receivedChunks, resp.totalChunks);
    } else {
        // 无法恢复
        resp.canResume = 0;
        resp.totalChunks = 0;
        resp.receivedChunks = 0;
        responseBody.append((char*)&resp, sizeof(resp));

        LOG_INFO("发送恢复传输响应: canResume=false");
    }

    QByteArray packet = makePacket(MSG_FILE_RESUME_RESP, responseBody, 0, senderId);
    NetworkManager::instance().sendRow(packet);
}

void MainWindow::onFileResumeResp(const QString &fileId, bool canResume, int totalChunks, int receivedChunks, const QByteArray &bitmap)
{
    // 作为发送方，收到接收方的恢复传输响应
    LOG_INFO(QString("收到恢复传输响应: %1, canResume=%2, received=%3/%4").arg(fileId).arg(canResume).arg(receivedChunks).arg(totalChunks));

    TransferState state = TransferStateManager::instance().loadTransferState(fileId);
    if(state.fileId.isEmpty()){
        LOG_WARN_FMT("无法加载传输状态: %1", fileId);
        return;
    }

    if(canResume){
        // 解析位图，更新已完成分片
        QSet<int> completedChunks;
        for(int i = 0; i < totalChunks; ++i){
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            if(byteIndex < bitmap.size()){
                if(bitmap[byteIndex] & (1 << bitIndex)){
                    completedChunks.insert(i);
                }
            }
        }

        // 更新状态
        state.completedChunks = completedChunks;
        TransferStateManager::instance().saveTransferState(state);

        // 显示提示信息
        QString displayText = QString("[恢复传输] %1 (已完成 %2/%3 分片)")
                                  .arg(state.fileName).arg(receivedChunks).arg(totalChunks);
        ChatMessage msg(displayText, true, ":/res/me.jpg");
        m_chatModel->addMessage(msg);
        ui->chatList->scrollToBottom();

        // 开始传输（FileTransferManager会自动跳过已完成的分片）
        FileTransferManager::instance().startSendFile(fileId, state.filePath, state.friendId);
    } else {
        // 无法恢复，需要重新开始
        LOG_WARN_FMT("对方无法恢复传输: %1", fileId);

        QString msg = QString("对方无法恢复文件 %1 的传输，是否重新发送？").arg(state.fileName);
        auto res = QMessageBox::question(this, "恢复传输失败", msg, QMessageBox::Yes | QMessageBox::No);

        if(res == QMessageBox::Yes){
            // 清除旧状态，重新发送
            TransferStateManager::instance().removeTransferState(fileId);
            sendFileTransferRequestForResume(fileId, state, state.friendId);
        }
    }
}

void MainWindow::on_btnSend_clicked()
{
    QString text = ui->msgEdit->toPlainText();
    if (text.isEmpty()) {
        return;
    }

    // 判断是群聊还是私聊
    if (m_isGroupChat && m_currentGroupId > 0) {
        // 群聊发送 - 需要加密
        GroupChatMessage header;
        header.groupId = m_currentGroupId;
        header.senderId = m_currentUserId;
        strncpy(header.senderName, m_currentUserName.toUtf8().constData(), 31);
        header.senderName[31] = '\0';

        // 获取或生成群聊密钥
        QByteArray key = EncryptionManager::instance().getCachedGroupKey(m_currentGroupId);
        
        if (key.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to get group encryption key");
            QMessageBox::warning(this, "发送失败", "无法生成群聊加密密钥");
            return;
        }
        
        // 对消息内容进行XOR加密
        QByteArray plaintext = text.toUtf8();
        QByteArray encrypted = EncryptionManager::instance().xorEncryptDecrypt(plaintext, key);
        
        if (encrypted.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to encrypt group message");
            QMessageBox::warning(this, "发送失败", "群消息加密失败");
            return;
        }
        
        // 组装消息内容：带加密标记的子类型 + 加密后的内容
        QByteArray msgContent;
        msgContent.append(addEncryptedFlag(SUB_TEXT));
        msgContent.append(encrypted);

        QByteArray body;
        body.append((char*)&header, sizeof(GroupChatMessage));
        body.append(msgContent);
        
        LOG_DEBUG(QString("[MainWindow] Sending encrypted group message to group %1 (plaintext: %2 bytes, encrypted: %3 bytes)")
                 .arg(m_currentGroupId).arg(plaintext.size()).arg(encrypted.size()));

        NetworkManager::instance().sendMsg(MSG_GROUP_CHAT_TEXT, body);

        // 本地显示（群聊消息也显示自己的用户名）
        ChatMessage msg(text, true, ":/res/me.jpg", m_currentUserName);
        m_chatModel->addMessage(msg);
        
        // 更新最后消息时间（群聊ID为负数）
        updateContactLastMsgTime(-m_currentGroupId, QDateTime::currentDateTime());
    } else if (m_currentFriendId > 0) {
        // 私聊发送 - 需要加密
        QByteArray body;
        
        // 获取或生成聊天密钥
        QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, m_currentFriendId);
        
        if (key.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to get chat encryption key");
            QMessageBox::warning(this, "发送失败", "无法生成加密密钥");
            return;
        }
        
        // 对消息内容进行XOR加密
        QByteArray plaintext = text.toUtf8();
        QByteArray encrypted = EncryptionManager::instance().xorEncryptDecrypt(plaintext, key);
        
        if (encrypted.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to encrypt message");
            QMessageBox::warning(this, "发送失败", "消息加密失败");
            return;
        }
        
        // 使用带加密标记的子类型
        body.append(addEncryptedFlag(SUB_TEXT));
        body.append(encrypted);
        
        LOG_DEBUG(QString("[MainWindow] Sending encrypted message to user %1 (plaintext: %2 bytes, encrypted: %3 bytes)")
                 .arg(m_currentFriendId).arg(plaintext.size()).arg(encrypted.size()));

        QByteArray packet = makePacket(MSG_CHAT_TEXT, body, 0, m_currentFriendId);
        NetworkManager::instance().sendRow(packet);

        ChatMessage msg(text, true, ":/res/me.jpg");
        m_chatModel->addMessage(msg);
        
        // 更新最后消息时间
        updateContactLastMsgTime(m_currentFriendId, QDateTime::currentDateTime());
    } else {
        return;
    }

    ui->chatList->scrollToBottom();
    ui->msgEdit->clear();
}

void MainWindow::setCurrentUserId(int newCurrentUserId)
{
    m_currentUserId = newCurrentUserId;
    
    // 同时设置FileReceiver的当前用户ID（用于文件解密）
    FileReceiver::instance().setCurrentUserId(newCurrentUserId);
}

void MainWindow::setCurrentUserName(const QString &name)
{
    m_currentUserName = name;
}

void MainWindow::on_btnContact_clicked()
{
    if(ui->stackedWidget->currentIndex() != 0){
        ui->stackedWidget->setCurrentIndex(0);
        ui->searchEdit->clear();
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ,QByteArray());
    }
    
    // 点击好友按钮时，隐藏聊天内容但保留背景
    ui->lblChatTitle->setVisible(false);
    ui->chatList->setVisible(false);
    ui->msgEdit->setVisible(false);
    ui->btnSend->setVisible(false);
    ui->btnImage->setVisible(false);
    ui->btnFile->setVisible(false);
    
    // 切换到好友模式，不显示时间
    updateContactListMode(false);
}

void MainWindow::on_btnChat_clicked()
{
    // 点击会话按钮时，显示好友列表
    if(ui->stackedWidget->currentIndex() != 0){
        ui->stackedWidget->setCurrentIndex(0);
        ui->searchEdit->clear();
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ,QByteArray());
    }
    
    // 显示聊天内容（如果之前选择过好友）
    if(m_currentFriendId > 0 || m_currentGroupId > 0){
        ui->lblChatTitle->setVisible(true);
        ui->chatList->setVisible(true);
        ui->msgEdit->setVisible(true);
        ui->btnSend->setVisible(true);
        ui->btnImage->setVisible(true);
        ui->btnFile->setVisible(true);
    }
    
    // 切换到会话模式，显示时间
    updateContactListMode(true);
}

void MainWindow::on_btnNewFriends_clicked()
{
    // 只有当从其他页面切换到新朋友页面时，才清除未读标记
    bool wasOnDifferentPage = (ui->stackedWidget->currentIndex() != 1);
    
    if(wasOnDifferentPage){
        ui->stackedWidget->setCurrentIndex(1);
        updateNewFriendsPage();
        
        // 切换到新朋友页面时，清除未读标记
        m_hasUnreadFriendRequests = false;
        updateNewFriendsButtonState();
    }
}

void MainWindow::on_btnImage_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择图片", "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)"
        );
    if (filePath.isEmpty()) return;

    // 检查是否在群聊或私聊模式
    if (!m_isGroupChat && m_currentFriendId == 0) {
        QMessageBox::warning(this, "提示", "请先选择一个好友或群聊");
        return;
    }

    QImage image(filePath);
    if (image.isNull()) {
        QMessageBox::warning(this, "错误", "无法加载图片");
        return;
    }

    // 限制大小
    if (image.width() > 800) {
        image = image.scaledToWidth(800, Qt::SmoothTransformation);
    }

    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 85); // 压缩质量 85

    // 判断是群聊还是私聊
    if (m_isGroupChat && m_currentGroupId > 0) {
        // 群聊发送图片 - 需要加密
        GroupChatMessage header;
        header.groupId = m_currentGroupId;
        header.senderId = m_currentUserId;
        strncpy(header.senderName, m_currentUserName.toUtf8().constData(), 31);
        header.senderName[31] = '\0';

        // 获取或生成群聊密钥
        QByteArray key = EncryptionManager::instance().getCachedGroupKey(m_currentGroupId);
        
        if (key.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to get group encryption key for image");
            QMessageBox::warning(this, "发送失败", "无法生成群聊加密密钥");
            return;
        }
        
        // 对图片数据进行XOR加密
        QByteArray encrypted = EncryptionManager::instance().xorEncryptDecrypt(imageData, key);
        
        if (encrypted.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to encrypt group image");
            QMessageBox::warning(this, "发送失败", "群图片加密失败");
            return;
        }
        
        // 组装消息内容：带加密标记的子类型 + 加密后的内容
        QByteArray msgContent;
        msgContent.append(addEncryptedFlag(SUB_IMAGE));
        msgContent.append(encrypted);

        QByteArray body;
        body.append((char*)&header, sizeof(GroupChatMessage));
        body.append(msgContent);
        
        LOG_DEBUG(QString("[MainWindow] Sending encrypted group image to group %1 (plaintext: %2 bytes, encrypted: %3 bytes)")
                 .arg(m_currentGroupId).arg(imageData.size()).arg(encrypted.size()));

        NetworkManager::instance().sendMsg(MSG_GROUP_CHAT_TEXT, body);

        // 本地显示（群聊消息也显示自己的用户名）
        ChatMessage msg(imageData, true, ":/res/me.jpg", m_currentUserName);
        m_chatModel->addMessage(msg);
    } else if (m_currentFriendId > 0) {
        // 私聊发送图片 - 需要加密
        QByteArray body;
        
        // 获取或生成聊天密钥
        QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, m_currentFriendId);
        
        if (key.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to get chat encryption key for image");
            QMessageBox::warning(this, "发送失败", "无法生成加密密钥");
            return;
        }
        
        // 对图片数据进行XOR加密
        QByteArray encrypted = EncryptionManager::instance().xorEncryptDecrypt(imageData, key);
        
        if (encrypted.isEmpty()) {
            LOG_ERROR("[MainWindow] Failed to encrypt image");
            QMessageBox::warning(this, "发送失败", "图片加密失败");
            return;
        }
        
        // 使用带加密标记的子类型
        body.append(addEncryptedFlag(SUB_IMAGE));
        body.append(encrypted);
        
        LOG_DEBUG(QString("[MainWindow] Sending encrypted image to user %1 (plaintext: %2 bytes, encrypted: %3 bytes)")
                 .arg(m_currentFriendId).arg(imageData.size()).arg(encrypted.size()));

        QByteArray packet = makePacket(MSG_CHAT_TEXT, body, 0, m_currentFriendId);
        NetworkManager::instance().sendRow(packet);

        // 本地立即显示（显示原始未加密的图片）
        ChatMessage msg(imageData, true, ":/res/me.jpg");
        m_chatModel->addMessage(msg);
    } else {
        return;
    }

    ui->chatList->scrollToBottom();
}

void MainWindow::on_btnFile_clicked()
{
    if(m_currentFriendId == 0){
        QMessageBox::warning(this, "提示", "请先选择一个好友");
        return;
    }

    // 获取要发送的文件路径
    QString filePath = QFileDialog::getOpenFileName(this,"选择文件","","所有文件(*.*)");

    if(filePath.isEmpty()){
        return;
    }

    // 获取文件信息
    QFileInfo fileInfo(filePath);

    // 先限制文件大小为100M
    if(fileInfo.size() > 100 * 1024 * 1024){
        QMessageBox::warning(this,"文件太大","文件大小不能超过100M");
        return;
    }

    // 先生成文件id，等对方同意后再传输文件
    QString fileId = FileTransferManager::instance().generateFileId(filePath);
    m_pendingFileTransfers[fileId] = filePath;

    // 先发送文件传输请求给对方
    FileTransferReq req;
    memset(&req,0,sizeof(FileTransferReq));

    // 文件名和文件大小
    strncpy(req.fileName,fileInfo.fileName().toUtf8().constData(),255);
    req.fileName[255] = '\0';
    req.fileSize = fileInfo.size();

    // 分片总数（每片64KB）
    quint64 chunkSize = 64 * 1024;
    req.totalChunks = (fileInfo.size() + chunkSize - 1) / chunkSize;

    strncpy(req.fileId,fileId.toUtf8().constData(),63);
    req.fileId[63] = '\0';

    // 发送请求
    QByteArray packet = makePacket(MSG_FILE_TRANSFER_REQ,QByteArray((char*)&req,sizeof(FileTransferReq)),0,m_currentFriendId);
    NetworkManager::instance().sendRow(packet);
}


void MainWindow::on_btnGroup_clicked()
{
    // 获取好友数据（只包含好友，排除群聊）
    QList<FriendSelectInfo> friendList;
    int count = ui->contactList->count();
    for (int i = 0; i < count; ++i) {
        QListWidgetItem *item = ui->contactList->item(i);
        int uid = item->data(ContactDelegate::RoleStatus).toInt();
        
        // 只添加好友（ID为正数），排除群聊（ID为负数）和自己（ID为0或负数）
        if(uid > 0){
            QString name = item->data(ContactDelegate::RoleName).toString();
            friendList.append({uid,name});
        }
    }

    // 弹出创建群聊窗口
    GroupDialog groupDialog(friendList,this);
    if(groupDialog.exec() == QDialog::Accepted){
        QString groupName = groupDialog.getGroupName();
        QList<int> memberIds = groupDialog.getSelctFriendIds();

        // 发送创建群聊请求
        CreateGroupReq req;
        memset(&req, 0, sizeof(CreateGroupReq));
        strncpy(req.groupName, groupName.toUtf8().constData(), 63);
        req.groupName[63] = '\0';

        // 保存待邀请的成员列表（创建成功后邀请）
        m_pendingGroupMembers = memberIds;

        NetworkManager::instance().sendMsg(MSG_CREATE_GROUP_REQ, QByteArray((char*)&req, sizeof(CreateGroupReq)));
    }
}




void MainWindow::onContactListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = ui->contactList->itemAt(pos);
    if (!item) {
        return;
    }

    // 检查是否是好友（不是搜索结果）
    bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();
    if (!isFriend) {
        return; // 只对好友和群聊显示菜单
    }

    // 检查是否是群聊（群聊ID为负数）
    int contactId = item->data(ContactDelegate::RoleStatus).toInt();
    
    // 只在好友模式下显示右键菜单
    if (ui->btnContact->isChecked()) {
        // 创建上下文菜单
        QMenu contextMenu(this);
        
        if (contactId < 0) {
            // 群聊：显示退出群聊选项
            QAction *leaveGroupAction = contextMenu.addAction("退出群聊");
            leaveGroupAction->setIcon(QIcon(":/icons/leave.png")); // 如果有图标的话
            
            // 显示菜单并获取用户选择
            QAction *selectedAction = contextMenu.exec(ui->contactList->mapToGlobal(pos));
            
            if (selectedAction == leaveGroupAction) {
                // 弹出确认对话框
                QString groupName = item->data(ContactDelegate::RoleName).toString();
                int ret = QMessageBox::question(this, "退出群聊", 
                                               QString("确定要退出群聊 \"%1\" 吗？\n退出后将无法收到群聊消息。").arg(groupName),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
                
                if (ret == QMessageBox::Yes) {
                    // 发送退出群聊请求
                    LeaveGroupReq req;
                    req.groupId = -contactId; // 转换为正数的群ID
                    
                    NetworkManager::instance().sendMsg(MSG_LEAVE_GROUP_REQ, QByteArray((char*)&req, sizeof(LeaveGroupReq)));
                }
            }
        } else {
            // 好友：显示删除好友选项
            QAction *deleteAction = contextMenu.addAction("删除好友");
            deleteAction->setIcon(QIcon(":/icons/delete.png")); // 如果有图标的话

            // 显示菜单并获取用户选择
            QAction *selectedAction = contextMenu.exec(ui->contactList->mapToGlobal(pos));
            
            if (selectedAction == deleteAction) {
                // 弹出确认对话框
                QString friendName = item->data(ContactDelegate::RoleName).toString();
                int ret = QMessageBox::question(this, "删除好友", 
                                               QString("确定要删除好友 \"%1\" 吗？\n删除后将无法收到对方的消息。").arg(friendName),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
                
                if (ret == QMessageBox::Yes) {
                    // 发送删除好友请求
                    DeleteFriendReq req;
                    req.targetId = contactId;
                    
                    NetworkManager::instance().sendMsg(MSG_DELETE_FRIEND_REQ, QByteArray((char*)&req, sizeof(DeleteFriendReq)));
                }
            }
        }
    }
    // 如果是会话模式（btnChat->isChecked()），则不显示任何右键菜单
}

void MainWindow::onSigDeleteFriendResponse(int result, int targetId)
{
    if (result == 1) {
        // 删除成功
        QMessageBox::information(this, "删除好友", "好友删除成功！");
        
        // 刷新好友列表和群聊列表
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ, QByteArray());
        
        // 如果当前正在与被删除的好友聊天，清空聊天窗口
        if (m_currentFriendId == targetId) {
            m_chatModel->clearMessages();
            ui->lblChatTitle->setText("选择一个好友开始聊天");
            m_currentFriendId = 0;
        }
    } else {
        // 删除失败
        QMessageBox::warning(this, "删除好友", "删除好友失败，请稍后重试。");
    }
}
void MainWindow::onSigLeaveGroupResponse(int result, int groupId)
{
    if (result == 1) {
        // 退出成功
        QMessageBox::information(this, "退出群聊", "成功退出群聊！");
        
        // 刷新好友列表和群聊列表
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ, QByteArray());
        NetworkManager::instance().sendMsg(MSG_GROUP_LIST_REQ, QByteArray());
        
        // 如果当前正在与退出的群聊聊天，清空聊天窗口
        if (m_currentGroupId == groupId) {
            m_chatModel->clearMessages();
            ui->lblChatTitle->setText("选择一个好友开始聊天");
            m_currentGroupId = 0;
            m_isGroupChat = false;
        }
    } else {
        // 退出失败
        QMessageBox::warning(this, "退出群聊", "退出群聊失败，请稍后重试。");
    }
}