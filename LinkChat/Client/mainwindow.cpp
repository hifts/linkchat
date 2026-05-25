#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "networkmanager.h"
#include "filetransfermanager.h"
#include "filetransferconstants.h"
#include "filereceiver.h"
#include "groupdialog.h"
#include "logger.h"
#include "encryptionmanager.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFontMetrics>
#include <QtMath>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QSplitter>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QTextDocument>

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
    saveFileMessageHistory();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    syncCurrentChatModelToCache();
    saveFileMessageHistory();

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

void MainWindow::onLogoutRequested()
{
    if (QMessageBox::question(this, "退出登录", "确定要退出当前账号并返回登录页吗？",
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    syncCurrentChatModelToCache();
    saveFileMessageHistory();
    EncryptionManager::instance().clearKeyCache();
    NetworkManager::instance().disconnectFromServer();
    emit logoutRequested();
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionNewFriendsBadge();
    positionEmptyStateLabels();
    positionWindowControlButtons();
    updateWindowMaximizeButton();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        if (watched == ui->contactList->viewport()
            || watched == ui->friendRequestsList->viewport()
            || watched == ui->chatList->viewport()) {
            positionEmptyStateLabels();
        }
    }

    if (event->type() == QEvent::Wheel) {
        if (watched == ui->contactList->viewport()) {
            showScrollBarTemporarily(ui->contactList);
        } else if (watched == ui->friendRequestsList->viewport()) {
            showScrollBarTemporarily(ui->friendRequestsList);
        } else if (watched == ui->chatList->viewport()) {
            showScrollBarTemporarily(ui->chatList);
        }
    }

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

void MainWindow::showScrollBarTemporarily(QAbstractItemView *view)
{
    if (!view || !view->verticalScrollBar()) {
        return;
    }

    if (m_activeScrollView && m_activeScrollView != view) {
        QScrollBar *previousBar = m_activeScrollView->verticalScrollBar();
        previousBar->setProperty("active", false);
        previousBar->style()->unpolish(previousBar);
        previousBar->style()->polish(previousBar);
        previousBar->update();
    }

    m_activeScrollView = view;
    QScrollBar *bar = view->verticalScrollBar();
    bar->setProperty("active", true);
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
    bar->update();

    if (m_scrollBarHideTimer) {
        m_scrollBarHideTimer->start();
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
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    ui->stackedWidget->setCurrentIndex(0);
    ui->stackedWidget->setMinimumWidth(260);
    ui->stackedWidget->setMaximumWidth(420);
    ui->listArea->setMinimumWidth(260);
    ui->listArea->setMaximumWidth(420);

    if (!m_contentSplitter) {
        QLayoutItem *listItem = ui->horizontalLayout_2->takeAt(1);
        QLayoutItem *chatItem = ui->horizontalLayout_2->takeAt(1);

        if (listItem && chatItem) {
            m_contentSplitter = new QSplitter(Qt::Horizontal, this);
            m_contentSplitter->setObjectName("contentSplitter");
            m_contentSplitter->setChildrenCollapsible(false);
            m_contentSplitter->setHandleWidth(6);

            m_contentSplitter->addWidget(ui->stackedWidget);
            m_contentSplitter->addWidget(ui->chatArea);
            m_contentSplitter->setStretchFactor(0, 0);
            m_contentSplitter->setStretchFactor(1, 1);
            m_contentSplitter->setSizes({300, 620});

            ui->horizontalLayout_2->insertWidget(1, m_contentSplitter, 1);
        }

        delete listItem;
        delete chatItem;
    }

    createWindowControlButtons();
    ui->verticalLayout_3->setContentsMargins(4, 34, 2, 0);
    ui->lblChatTitle->setFixedHeight(25);

    // 创建按钮组实现互斥选中效果
    QButtonGroup *navButtonGroup = new QButtonGroup(this);
    navButtonGroup->setExclusive(true);  // 设置互斥，同一时间只能选中一个
    navButtonGroup->addButton(ui->btnAvatar);
    navButtonGroup->addButton(ui->btnChat);
    navButtonGroup->addButton(ui->btnContact);
    navButtonGroup->addButton(ui->btnNewFriends);

    ui->btnAvatar->setText("我");
    ui->btnAvatar->setToolTip("当前账号");
    ui->btnAvatar->setCheckable(true);

    m_btnLogout = new QPushButton("退出登录", ui->sideBar);
    m_btnLogout->setObjectName("btnLogout");
    m_btnLogout->setToolTip("退出当前账号并返回登录页");
    m_btnLogout->setFocusPolicy(Qt::NoFocus);
    ui->verticalLayout->addWidget(m_btnLogout);
    connect(m_btnLogout, &QPushButton::clicked, this, &MainWindow::onLogoutRequested);

    m_newFriendsBadge = new QLabel(ui->btnNewFriends);
    m_newFriendsBadge->setObjectName("newFriendsBadge");
    m_newFriendsBadge->setFixedSize(4, 4);
    positionNewFriendsBadge();
    m_newFriendsBadge->raise();
    m_newFriendsBadge->hide();

    m_contactEmptyLabel = new QLabel(ui->contactList->viewport());
    m_contactEmptyLabel->setObjectName("emptyStateLabel");
    m_contactEmptyLabel->setAlignment(Qt::AlignCenter);
    m_contactEmptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_contactEmptyLabel->hide();

    m_friendRequestsEmptyLabel = new QLabel(ui->friendRequestsList->viewport());
    m_friendRequestsEmptyLabel->setObjectName("emptyStateLabel");
    m_friendRequestsEmptyLabel->setAlignment(Qt::AlignCenter);
    m_friendRequestsEmptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_friendRequestsEmptyLabel->setText("暂时没有新的好友请求");
    m_friendRequestsEmptyLabel->hide();

    m_chatEmptyLabel = new QLabel(ui->chatArea);
    m_chatEmptyLabel->setObjectName("emptyStateLabel");
    m_chatEmptyLabel->setAlignment(Qt::AlignCenter);
    m_chatEmptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_chatEmptyLabel->hide();

    // 默认选中"会话"按钮，并隐藏聊天内容
    ui->btnChat->setChecked(true);
    ui->lblChatTitle->setVisible(false);
    ui->chatList->setVisible(false);
    ui->msgEdit->setVisible(false);
    ui->btnSend->setVisible(false);
    ui->btnImage->setVisible(false);
    ui->btnFile->setVisible(false);

    ui->chatList->setMouseTracking(true);
    ui->chatList->viewport()->setAttribute(Qt::WA_Hover);
    ui->msgEdit->setAcceptRichText(false);
    ui->msgEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    ui->msgEdit->setMinimumHeight(44);
    ui->msgEdit->setMaximumHeight(96);
    ui->msgEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->msgEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->btnSend->setFixedSize(64, 36);
    ui->horizontalLayout->setAlignment(ui->btnSend, Qt::AlignBottom);
    ui->horizontalLayout->setAlignment(ui->btnImage, Qt::AlignBottom);
    ui->horizontalLayout->setAlignment(ui->btnFile, Qt::AlignBottom);
    updateMessageInputHeight();

    QFile styleFile(":/styles/dark.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        this->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        LOG_WARN("Failed to load stylesheet: :/styles/dark.qss");
    }
    updateWindowMaximizeButton();
    updateEmptyStates();
}

void MainWindow::initModel()
{
    m_chatModel = new ChatModel(this);
    m_chatDelegate = new ChatDelegate(this);
    m_contactDelegate = new ContactDelegate(this);

    // 绑定模型到QListView
    ui->chatList->setModel(m_chatModel);
    ui->chatList->setItemDelegate(m_chatDelegate);
    connect(m_chatModel, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateEmptyStates);
    connect(m_chatModel, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateEmptyStates);
    connect(m_chatModel, &QAbstractItemModel::modelReset, this, &MainWindow::updateEmptyStates);

    ui->chatList->setResizeMode(QListView::Adjust);
    ui->chatList->setSpacing(5);
    ui->chatList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->chatList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui->chatList->verticalScrollBar()->setSingleStep(10);
    ui->chatList->viewport()->installEventFilter(this);
    ui->chatList->verticalScrollBar()->setProperty("active", false);
    ui->chatList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->chatList, &QListView::customContextMenuRequested, this, &MainWindow::onChatListContextMenu);
    connect(ui->chatList, &QListView::doubleClicked, this, &MainWindow::onChatListDoubleClicked);

    // 好友列表代理
    ui->contactList->setItemDelegate(m_contactDelegate);
    ui->contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->contactList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui->contactList->verticalScrollBar()->setSingleStep(8);
    ui->contactList->viewport()->installEventFilter(this);
    ui->contactList->verticalScrollBar()->setProperty("active", false);

    ui->friendRequestsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->friendRequestsList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui->friendRequestsList->verticalScrollBar()->setSingleStep(8);
    ui->friendRequestsList->viewport()->installEventFilter(this);
    ui->friendRequestsList->verticalScrollBar()->setProperty("active", false);
    positionEmptyStateLabels();

    m_scrollBarHideTimer = new QTimer(this);
    m_scrollBarHideTimer->setInterval(800);
    m_scrollBarHideTimer->setSingleShot(true);
    connect(m_scrollBarHideTimer, &QTimer::timeout, this, [this]() {
        if (m_activeScrollView) {
            QScrollBar *bar = m_activeScrollView->verticalScrollBar();
            bar->setProperty("active", false);
            bar->style()->unpolish(bar);
            bar->style()->polish(bar);
            bar->update();
            m_activeScrollView.clear();
        }
    });

    const QList<QAbstractItemView*> scrollViews = {ui->contactList, ui->friendRequestsList, ui->chatList};
    for (QAbstractItemView *view : scrollViews) {
        connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, view]() {
            showScrollBarTemporarily(view);
        });
    }

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

    // 监听搜索框
    connect(ui->searchEdit,&QLineEdit::textChanged,this,&::MainWindow::onSearchTextChanged);
    connect(ui->msgEdit, &QTextEdit::textChanged, this, &MainWindow::updateMessageInputHeight);

    connect(ui->contactList, &QListWidget::itemClicked, this, &MainWindow::onContactListClicked);

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
    connect(&NetworkManager::instance(),&NetworkManager::sigFileTransferAck,this,&MainWindow::onFileTransferAck);
    connect(&NetworkManager::instance(),&NetworkManager::sigFileTransferAckBatch,this,&MainWindow::onFileTransferAckBatch);
    connect(&FileReceiver::instance(),&FileReceiver::receiveProgress,this,&MainWindow::onFileReceiveProgress);

    connect(&FileReceiver::instance(),&FileReceiver::receiveCompleted,
    this,[this](const QString &fileId,const QString &savePath){
        try {
            // 移除活动传输记录
            ReconnectTransferManager::instance().removeCompletedTransfer(fileId);
            onFileReceiveCompleted(fileId,savePath);
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in receiveCompleted signal handler: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in receiveCompleted signal handler");
        }
    });

    connect(&FileReceiver::instance(),&FileReceiver::receiveFailed,this,&MainWindow::onFileReceiveFailed);

    // 关联断线重连传输信号
    connect(&ReconnectTransferManager::instance(),&::ReconnectTransferManager::readyToResumeTransfer,this,&MainWindow::onReadyToResumeTransfers);
    connect(&ReconnectTransferManager::instance(),&ReconnectTransferManager::requestResumeTransfer,this,&MainWindow::onRequestResumeTransfer);

    // 关联断点续传协议信号
    connect(&NetworkManager::instance(),&NetworkManager::sigFileResumeReq,this,&MainWindow::onFileResumeReq);
    connect(&NetworkManager::instance(),&NetworkManager::sigFileResumeResp,this,&MainWindow::onFileResumeResp);
    connect(&NetworkManager::instance(),&NetworkManager::sigFileTransferCanceled,this,&MainWindow::onFileTransferCanceled);

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
    if (accepted) {
        m_sessionVisibleFriendIds.insert(requesterId);
    }
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

    quint64 chunkSize = FILE_TRANSFER_CHUNK_SIZE;
    req.totalChunks = (state.fileSize + chunkSize - 1) / chunkSize;

    strncpy(req.fileId, fileId.toUtf8().constData(), sizeof(req.fileId) - 1);
    req.fileId[63] = '\0';

    // 发送请求
    QByteArray packet = makePacket(MSG_FILE_TRANSFER_REQ, QByteArray((char*)&req, sizeof(FileTransferReq)),0,friendId);
    NetworkManager::instance().sendRow(packet);

    LOG_INFO(QString("重新发送文件传输请求: %1 (已完成 %2/%3 分片)").arg(state.fileName).arg(state.completedChunks.size()).arg(state.totalChunks));

    // 待处理列表添加
    m_pendingFileTransfers[fileId] = state.filePath;
    m_pendingFileTransferTargets[fileId] = friendId;
    m_pendingFileTransferSizes[fileId] = state.fileSize;
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

                    const QString detail = QString("请求恢复 · 已完成 %1/%2 分片")
                                               .arg(state.completedChunks.size())
                                               .arg(state.totalChunks);
                    appendChatMessage(state.friendId, makeFileMessage(state.fileId, state.fileName, detail, true));
                } else {
                    LOG_WARN_FMT("文件不存在，无法恢复: %1", state.filePath);
                    TransferStateManager::instance().removeTransferState(state.fileId);
                }
            } else {
                // 接收端：等待对方继续发送
                ReconnectTransferManager::instance().saveActiveTransfer(
                    state.fileId, state.fileName, state.friendId, false);

                const int chatId = state.friendId > 0 ? state.friendId : m_currentFriendId;
                const int progress = state.totalChunks > 0
                    ? (state.completedChunks.size() * 100) / state.totalChunks
                    : 0;
                const QString detail = QString("等待恢复 · %1%").arg(progress);
                appendChatMessage(chatId, makeFileMessage(state.fileId, state.fileName, detail, false));
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

void MainWindow::updateContactLastMessagePreview(int chatId, const QString &preview, const QDateTime &time)
{
    if (chatId == 0) {
        return;
    }

    updateContactLastMsgTime(chatId, time);
    if (chatId > 0) {
        m_sessionVisibleFriendIds.insert(chatId);
    }

    for (int i = 0; i < ui->contactList->count(); ++i) {
        QListWidgetItem *item = ui->contactList->item(i);
        if (item->data(ContactDelegate::RoleStatus).toInt() != chatId) {
            continue;
        }

        item->setData(ContactDelegate::RoleLastMessagePreview,
                      item->data(ContactDelegate::RoleShowTime).toBool() ? preview : QString());
        ui->contactList->viewport()->update();
        break;
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
    ui->btnNewFriends->setText("🔔 新朋友");
    if (m_newFriendsBadge) {
        positionNewFriendsBadge();
        m_newFriendsBadge->setVisible(m_hasUnreadFriendRequests);
        m_newFriendsBadge->raise();
    }
}

void MainWindow::positionNewFriendsBadge()
{
    if (!m_newFriendsBadge) {
        return;
    }

    QFontMetrics metrics(ui->btnNewFriends->font());
    const int textWidth = metrics.horizontalAdvance(ui->btnNewFriends->text());
    const int x = ui->btnNewFriends->contentsMargins().left() + 12 + textWidth + 3;
    const int y = (ui->btnNewFriends->height() - m_newFriendsBadge->height()) / 2;
    m_newFriendsBadge->move(qMin(x, ui->btnNewFriends->width() - m_newFriendsBadge->width() - 8), qMax(0, y));
}

void MainWindow::createWindowControlButtons()
{
    if (m_btnWindowMinimize || m_btnWindowMaximize || m_btnWindowClose) {
        return;
    }

    auto setupButton = [this](const QString &objectName, const QString &text, const QString &tooltip) {
        QPushButton *button = new QPushButton(text, this);
        button->setObjectName(objectName);
        button->setToolTip(tooltip);
        button->setFixedSize(30, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    m_btnWindowMinimize = setupButton("btnWindowMinimize", "-", "最小化");
    m_btnWindowMaximize = setupButton("btnWindowMaximize", "□", "最大化");
    m_btnWindowClose = setupButton("btnWindowClose", "×", "关闭");

    connect(m_btnWindowMinimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(m_btnWindowMaximize, &QPushButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
        positionWindowControlButtons();
        updateWindowMaximizeButton();
    });
    connect(m_btnWindowClose, &QPushButton::clicked, this, &MainWindow::close);

    positionWindowControlButtons();
    updateWindowMaximizeButton();
}

void MainWindow::positionWindowControlButtons()
{
    if (!m_btnWindowMinimize || !m_btnWindowMaximize || !m_btnWindowClose) {
        return;
    }

    const int top = 8;
    const int right = 12;
    const int spacing = 4;
    const int buttonWidth = m_btnWindowClose->width();
    const int xClose = width() - right - buttonWidth;
    const int xMaximize = xClose - spacing - buttonWidth;
    const int xMinimize = xMaximize - spacing - buttonWidth;

    m_btnWindowMinimize->move(qMax(0, xMinimize), top);
    m_btnWindowMaximize->move(qMax(0, xMaximize), top);
    m_btnWindowClose->move(qMax(0, xClose), top);

    m_btnWindowMinimize->raise();
    m_btnWindowMaximize->raise();
    m_btnWindowClose->raise();
}

void MainWindow::updateWindowMaximizeButton()
{
    if (!m_btnWindowMaximize) {
        return;
    }

    m_btnWindowMaximize->setText(isMaximized() ? "❐" : "□");
    m_btnWindowMaximize->setToolTip(isMaximized() ? "还原" : "最大化");
}

void MainWindow::updateMessageInputHeight()
{
    if (!ui || !ui->msgEdit) {
        return;
    }

    const int minHeight = 44;
    const int maxHeight = 96;
    const int framePadding = 18;
    const int docHeight = qCeil(ui->msgEdit->document()->size().height()) + framePadding;
    const int targetHeight = qBound(minHeight, docHeight, maxHeight);

    ui->msgEdit->setFixedHeight(targetHeight);
    ui->msgEdit->setVerticalScrollBarPolicy(docHeight > maxHeight ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
}

QString MainWindow::conversationPreviewForMessage(const ChatMessage &msg) const
{
    switch (msg.type) {
    case TypeImage:
        return "[图片]";
    case TypeFile: {
        const QString fileName = msg.fileName.isEmpty() ? msg.content.section('\n', 0, 0) : msg.fileName;
        return QString("[文件] %1").arg(fileName.isEmpty() ? "未知文件" : fileName);
    }
    case TypeText:
    default:
        return msg.content.simplified();
    }
}

ChatMessage MainWindow::makeFileMessage(const QString &fileId, const QString &fileName, const QString &detail, bool mine) const
{
    const QString safeName = fileName.isEmpty() ? "未知文件" : fileName;
    const QString content = detail.isEmpty()
        ? safeName
        : QString("%1\n%2").arg(safeName, detail);

    ChatMessage msg(content, mine, mine ? ":/res/me.jpg" : ":/res/you.jpeg");
    msg.type = TypeFile;
    msg.fileId = fileId;
    msg.fileName = safeName;
    msg.fileDetail = detail;
    msg.fileProgress = detail.contains('%') ? detail.section('%', 0, 0).section(' ', -1).toInt() : -1;
    return msg;
}

void MainWindow::appendChatMessage(int chatId, const ChatMessage &msg, bool showImmediately)
{
    ChatMessage message = msg;
    if (message.timestamp == 0) {
        message.timestamp = QDateTime::currentSecsSinceEpoch();
    }

    bool replaced = false;
    if (chatId > 0) {
        if (message.type == TypeFile) {
            replaced = replaceFileMessageInList(m_chatHistory[chatId], message);
        }
        if (!replaced) {
            m_chatHistory[chatId].append(message);
        }
        normalizeMessageList(m_chatHistory[chatId]);
    } else if (chatId < 0) {
        if (message.type == TypeFile) {
            replaced = replaceFileMessageInList(m_groupChatHistory[-chatId], message);
        }
        if (!replaced) {
            m_groupChatHistory[-chatId].append(message);
        }
        normalizeMessageList(m_groupChatHistory[-chatId]);
    }

    const bool isCurrentPrivateChat = chatId > 0 && !m_isGroupChat && m_currentFriendId == chatId;
    const bool isCurrentGroupChat = chatId < 0 && m_isGroupChat && m_currentGroupId == -chatId;

    if (showImmediately && (isCurrentPrivateChat || isCurrentGroupChat)) {
        QList<ChatMessage> currentMessages = m_chatModel->getMessages();
        if (message.type == TypeFile && replaceFileMessageInList(currentMessages, message)) {
            normalizeMessageList(currentMessages);
            m_chatModel->setMessages(currentMessages);
        } else {
            m_chatModel->addMessage(message);
        }
        ui->chatList->scrollToBottom();
        updateEmptyStates();
    }

    const QList<ChatMessage> *history = nullptr;
    if (chatId > 0 && m_chatHistory.contains(chatId)) {
        history = &m_chatHistory[chatId];
    } else if (chatId < 0 && m_groupChatHistory.contains(-chatId)) {
        history = &m_groupChatHistory[-chatId];
    }

    if (history && !history->isEmpty()) {
        const ChatMessage &lastMessage = history->last();
        updateContactLastMessagePreview(chatId,
                                        conversationPreviewForMessage(lastMessage),
                                        QDateTime::fromSecsSinceEpoch(lastMessage.timestamp));
    }
    saveFileMessageHistory();
}

void MainWindow::syncCurrentChatModelToCache()
{
    if (!m_chatModel) {
        return;
    }

    if (m_isGroupChat && m_currentGroupId > 0) {
        m_groupChatHistory[m_currentGroupId] = m_chatModel->getMessages();
    } else if (!m_isGroupChat && m_currentFriendId > 0) {
        m_chatHistory[m_currentFriendId] = m_chatModel->getMessages();
    }
}

void MainWindow::normalizeMessageList(QList<ChatMessage> &messages) const
{
    QMap<QString, int> lastFileIndex;
    for (int i = 0; i < messages.size(); ++i) {
        const ChatMessage &msg = messages[i];
        if (msg.type == TypeFile && !msg.fileId.isEmpty()) {
            if (lastFileIndex.contains(msg.fileId)) {
                const int oldIndex = lastFileIndex.value(msg.fileId);
                if (messages[oldIndex].timestamp <= msg.timestamp) {
                    messages[oldIndex] = msg;
                }
                messages.removeAt(i);
                --i;
                continue;
            }
            lastFileIndex.insert(msg.fileId, i);
        }
    }

    std::stable_sort(messages.begin(), messages.end(), [](const ChatMessage &left, const ChatMessage &right) {
        return left.timestamp < right.timestamp;
    });
}

ChatMessage *MainWindow::findFileMessage(const QString &fileId, int *chatId)
{
    if (fileId.isEmpty()) {
        return nullptr;
    }

    for (auto it = m_chatHistory.begin(); it != m_chatHistory.end(); ++it) {
        for (ChatMessage &msg : it.value()) {
            if (msg.type == TypeFile && msg.fileId == fileId) {
                if (chatId) {
                    *chatId = it.key();
                }
                return &msg;
            }
        }
    }

    for (auto it = m_groupChatHistory.begin(); it != m_groupChatHistory.end(); ++it) {
        for (ChatMessage &msg : it.value()) {
            if (msg.type == TypeFile && msg.fileId == fileId) {
                if (chatId) {
                    *chatId = -it.key();
                }
                return &msg;
            }
        }
    }

    return nullptr;
}

ChatMessage MainWindow::currentFileMessageAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.data(ChatModel::RoleType).toInt() != TypeFile) {
        return ChatMessage();
    }

    ChatMessage msg;
    msg.type = TypeFile;
    msg.content = index.data(ChatModel::RoleContent).toString();
    msg.isMine = index.data(ChatModel::RoleIsMine).toBool();
    msg.avatarPath = index.data(ChatModel::RoleAvatar).toString();
    msg.timestamp = index.data(ChatModel::RoleTimestamp).toULongLong();
    msg.senderName = index.data(ChatModel::RoleSenderName).toString();
    msg.isGroupChat = index.data(ChatModel::RoleIsGroupChat).toBool();
    msg.encryptionStatus = static_cast<EncryptionStatus>(index.data(ChatModel::RoleEncryptionStatus).toInt());
    msg.fileId = index.data(ChatModel::RoleFileId).toString();
    msg.fileName = index.data(ChatModel::RoleFileName).toString();
    msg.fileDetail = index.data(ChatModel::RoleFileDetail).toString();
    msg.filePath = index.data(ChatModel::RoleFilePath).toString();
    msg.fileSavePath = index.data(ChatModel::RoleFileSavePath).toString();
    msg.filePeerId = index.data(ChatModel::RoleFilePeerId).toInt();
    msg.fileProgress = index.data(ChatModel::RoleFileProgress).toInt();
    msg.fileTransferredBytes = index.data(ChatModel::RoleFileTransferredBytes).toLongLong();
    msg.fileTotalBytes = index.data(ChatModel::RoleFileTotalBytes).toLongLong();
    return msg;
}

QString MainWindow::fileMessageOpenPath(const ChatMessage &msg) const
{
    if (!msg.fileSavePath.isEmpty()) {
        return msg.fileSavePath;
    }
    if (!msg.filePath.isEmpty()) {
        return msg.filePath;
    }

    const TransferState state = TransferStateManager::instance().loadTransferState(msg.fileId);
    if (!state.filePath.isEmpty()) {
        return state.filePath;
    }
    return state.tempFilePath;
}

QString MainWindow::formatTransferDetail(const QString &action, int percent, qint64 done, qint64 total) const
{
    QString detail = QString("%1 · %2%").arg(action).arg(qBound(0, percent, 100));
    if (total > 0) {
        detail += QString(" · %1/%2 KB")
                      .arg(done / 1024)
                      .arg(qMax<qint64>(1, total / 1024));
    }
    return detail;
}

bool MainWindow::replaceFileMessageInList(QList<ChatMessage> &messages, const ChatMessage &msg)
{
    if (msg.type != TypeFile || msg.fileId.isEmpty()) {
        return false;
    }

    for (int i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].type == TypeFile && messages[i].fileId == msg.fileId) {
            ChatMessage merged = msg;
            if (merged.timestamp == 0 || merged.timestamp < messages[i].timestamp) {
                merged.timestamp = QDateTime::currentSecsSinceEpoch();
            }
            if (merged.filePath.isEmpty()) {
                merged.filePath = messages[i].filePath;
            }
            if (merged.fileSavePath.isEmpty()) {
                merged.fileSavePath = messages[i].fileSavePath;
            }
            if (merged.filePeerId <= 0) {
                merged.filePeerId = messages[i].filePeerId;
            }
            if (merged.fileTotalBytes <= 0) {
                merged.fileTotalBytes = messages[i].fileTotalBytes;
            }
            messages[i] = merged;
            return true;
        }
    }

    return false;
}

bool MainWindow::updateFileMessageStatusInList(QList<ChatMessage> &messages, const QString &fileId, const QString &detail, int progress, qint64 done, qint64 total, const QString &savePath)
{
    if (fileId.isEmpty()) {
        return false;
    }

    for (int i = messages.size() - 1; i >= 0; --i) {
        ChatMessage &msg = messages[i];
        if (msg.type != TypeFile || msg.fileId != fileId) {
            continue;
        }

        const QString fileName = msg.fileName.isEmpty() ? msg.content.section('\n', 0, 0) : msg.fileName;
        msg.content = detail.isEmpty() ? fileName : QString("%1\n%2").arg(fileName, detail);
        msg.fileName = fileName;
        msg.fileDetail = detail;
        msg.fileProgress = progress;
        msg.fileTransferredBytes = done > 0 ? done : msg.fileTransferredBytes;
        msg.fileTotalBytes = total > 0 ? total : msg.fileTotalBytes;
        if (!savePath.isEmpty()) {
            msg.fileSavePath = savePath;
        }
        msg.timestamp = QDateTime::currentSecsSinceEpoch();
        return true;
    }

    return false;
}

bool MainWindow::updateFileMessageStatus(const QString &fileId, const QString &detail)
{
    return updateFileMessageProgress(fileId, detail, -1);
}

bool MainWindow::updateFileMessageProgress(const QString &fileId, const QString &detail, int progress, qint64 done, qint64 total, const QString &savePath)
{
    bool changed = false;

    for (auto it = m_chatHistory.begin(); it != m_chatHistory.end(); ++it) {
        changed = updateFileMessageStatusInList(it.value(), fileId, detail, progress, done, total, savePath) || changed;
    }
    for (auto it = m_groupChatHistory.begin(); it != m_groupChatHistory.end(); ++it) {
        changed = updateFileMessageStatusInList(it.value(), fileId, detail, progress, done, total, savePath) || changed;
    }

    if (m_chatModel) {
        QList<ChatMessage> currentMessages = m_chatModel->getMessages();
        if (updateFileMessageStatusInList(currentMessages, fileId, detail, progress, done, total, savePath)) {
            m_chatModel->setMessages(currentMessages);
            ui->chatList->scrollToBottom();
            updateEmptyStates();
            changed = true;
        }
    }

    if (changed) {
        int chatId = 0;
        if (ChatMessage *msg = findFileMessage(fileId, &chatId)) {
            const QString fileName = msg->fileName.isEmpty() ? msg->content.section('\n', 0, 0) : msg->fileName;
            updateContactLastMessagePreview(chatId, QString("[文件] %1").arg(fileName), QDateTime::fromSecsSinceEpoch(msg->timestamp));
        }
        saveFileMessageHistory();
    }

    return changed;
}

QString MainWindow::fileMessageHistoryPath() const
{
    if (m_currentUserId <= 0) {
        return QString();
    }

    const QString dirPath = QDir(QCoreApplication::applicationDirPath()).filePath("MessageHistory");
    QDir().mkpath(dirPath);
    return QDir(dirPath).filePath(QString("user_%1.json").arg(m_currentUserId));
}

void MainWindow::loadFileMessageHistory()
{
    const QString path = fileMessageHistoryPath();
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) {
        return;
    }

    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject obj = value.toObject();
        const int chatId = obj.value("chatId").toInt();
        if (chatId == 0) {
            continue;
        }

        ChatMessage msg;
        msg.type = static_cast<MsgType>(obj.value("type").toInt(TypeFile));
        msg.fileId = obj.value("fileId").toString();
        msg.content = obj.value("content").toString();
        msg.image = QByteArray::fromBase64(obj.value("imageBase64").toString().toLatin1());
        msg.fileName = obj.value("fileName").toString(msg.content.section('\n', 0, 0));
        msg.fileDetail = obj.value("fileDetail").toString(msg.content.section('\n', 1));
        msg.filePath = obj.value("filePath").toString();
        msg.fileSavePath = obj.value("fileSavePath").toString();
        msg.filePeerId = obj.value("filePeerId").toInt();
        msg.fileProgress = obj.value("fileProgress").toInt(-1);
        msg.fileTransferredBytes = static_cast<qint64>(obj.value("fileTransferredBytes").toDouble());
        msg.fileTotalBytes = static_cast<qint64>(obj.value("fileTotalBytes").toDouble());
        msg.isMine = obj.value("isMine").toBool();
        msg.avatarPath = obj.value("avatar").toString(msg.isMine ? ":/res/me.jpg" : ":/res/you.jpeg");
        msg.timestamp = static_cast<quint64>(obj.value("timestamp").toDouble());
        msg.senderName = obj.value("senderName").toString();
        msg.isGroupChat = obj.value("isGroupChat").toBool(false);
        msg.encryptionStatus = EncryptionUnknown;

        if ((msg.type == TypeText && msg.content.isEmpty())
            || (msg.type == TypeImage && msg.image.isEmpty())
            || (msg.type == TypeFile && (msg.content.isEmpty() || msg.fileId.isEmpty()))) {
            continue;
        }

        if (chatId > 0) {
            m_chatHistory[chatId].append(msg);
        } else {
            m_groupChatHistory[-chatId].append(msg);
        }
    }

    for (auto it = m_chatHistory.begin(); it != m_chatHistory.end(); ++it) {
        normalizeMessageList(it.value());
    }
    for (auto it = m_groupChatHistory.begin(); it != m_groupChatHistory.end(); ++it) {
        normalizeMessageList(it.value());
    }
}

void MainWindow::saveFileMessageHistory() const
{
    const QString path = fileMessageHistoryPath();
    if (path.isEmpty()) {
        return;
    }

    QJsonArray items;
    auto appendMessages = [&items](int chatId, const QList<ChatMessage> &messages) {
        for (const ChatMessage &msg : messages) {
            if (msg.type == TypeText && msg.content.isEmpty()) {
                continue;
            }
            if (msg.type == TypeImage && msg.image.isEmpty()) {
                continue;
            }
            if (msg.type == TypeFile && msg.fileId.isEmpty()) {
                continue;
            }

            QJsonObject obj;
            obj["chatId"] = chatId;
            obj["type"] = msg.type;
            obj["content"] = msg.content;
            if (msg.type == TypeImage) {
                obj["imageBase64"] = QString::fromLatin1(msg.image.toBase64());
            }
            obj["fileId"] = msg.fileId;
            obj["fileName"] = msg.fileName;
            obj["fileDetail"] = msg.fileDetail;
            obj["filePath"] = msg.filePath;
            obj["fileSavePath"] = msg.fileSavePath;
            obj["filePeerId"] = msg.filePeerId;
            obj["fileProgress"] = msg.fileProgress;
            obj["fileTransferredBytes"] = static_cast<double>(msg.fileTransferredBytes);
            obj["fileTotalBytes"] = static_cast<double>(msg.fileTotalBytes);
            obj["isMine"] = msg.isMine;
            obj["avatar"] = msg.avatarPath;
            obj["timestamp"] = static_cast<double>(msg.timestamp);
            obj["senderName"] = msg.senderName;
            obj["isGroupChat"] = msg.isGroupChat;
            items.append(obj);
        }
    };

    for (auto it = m_chatHistory.constBegin(); it != m_chatHistory.constEnd(); ++it) {
        appendMessages(it.key(), it.value());
    }
    for (auto it = m_groupChatHistory.constBegin(); it != m_groupChatHistory.constEnd(); ++it) {
        appendMessages(-it.key(), it.value());
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        LOG_WARN_FMT("Failed to save file message history: %1", path);
        return;
    }

    file.write(QJsonDocument(items).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::positionEmptyStateLabels()
{
    if (m_contactEmptyLabel) {
        m_contactEmptyLabel->setGeometry(ui->contactList->viewport()->rect());
        m_contactEmptyLabel->raise();
    }

    if (m_friendRequestsEmptyLabel) {
        m_friendRequestsEmptyLabel->setGeometry(ui->friendRequestsList->viewport()->rect());
        m_friendRequestsEmptyLabel->raise();
    }

    if (m_chatEmptyLabel) {
        QRect targetRect;
        if (ui->chatList->isVisible()) {
            targetRect = QRect(ui->chatList->pos(), ui->chatList->size());
        } else {
            targetRect = ui->chatArea->rect().adjusted(24, 58, -24, -24);
        }
        m_chatEmptyLabel->setGeometry(targetRect);
        m_chatEmptyLabel->raise();
    }
}

void MainWindow::updateEmptyStates()
{
    const bool contactPageVisible = ui->stackedWidget->currentWidget() == ui->page;
    const bool newFriendsPageVisible = ui->stackedWidget->currentWidget() == ui->page_2;
    const bool isSearching = !ui->searchEdit->text().trimmed().isEmpty();

    if (m_contactEmptyLabel) {
        QString text;
        if (isSearching) {
            text = "没有找到相关用户";
        } else if (ui->btnChat->isChecked()) {
            text = "暂无会话消息";
        } else {
            text = "暂无好友";
        }

        m_contactEmptyLabel->setText(text);
        m_contactEmptyLabel->setVisible(contactPageVisible && ui->contactList->count() == 0);
    }

    if (m_friendRequestsEmptyLabel) {
        m_friendRequestsEmptyLabel->setVisible(newFriendsPageVisible && ui->friendRequestsList->count() == 0);
    }

    if (m_chatEmptyLabel) {
        const bool hasSelectedChat = m_currentFriendId > 0 || m_currentGroupId > 0;
        m_chatEmptyLabel->setText(hasSelectedChat ? "暂无消息" : "选择一个会话开始聊天");
        const bool shouldShowChatEmpty = ui->btnChat->isChecked()
            && (!hasSelectedChat || (ui->chatList->isVisible() && m_chatModel && m_chatModel->rowCount(QModelIndex()) == 0));
        m_chatEmptyLabel->setVisible(shouldShowChatEmpty);
    }

    positionEmptyStateLabels();
}

void MainWindow::refreshContactList()
{
    ui->contactList->clear();
    m_friendIds.clear();
    m_groupIds.clear();

    const bool isSessionMode = ui->btnChat->isChecked();

    for (const auto &info : m_cachedFriendList) {
        m_friendIds.insert(info.id);

        if (isSessionMode && info.lastMsgTime == 0 && !m_sessionVisibleFriendIds.contains(info.id)) {
            continue;
        }

        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(300,60));

        // 在item里存放用户id和用户名
        item->setData(ContactDelegate::RoleStatus,info.id);
        item->setData(ContactDelegate::RoleName,QString::fromUtf8(info.userName));
        item->setData(ContactDelegate::RoleIsFriend, true);
        item->setData(ContactDelegate::RoleShowTime, isSessionMode); // 设置是否显示时间
        item->setData(ContactDelegate::RoleUnread, 0); // 初始化未读计数为0
        item->setData(ContactDelegate::RoleOnlineStatus, info.status);
        
        // 设置最后消息时间
        if (info.lastMsgTime > 0) {
            QDateTime lastTime = QDateTime::fromSecsSinceEpoch(info.lastMsgTime);
            item->setData(ContactDelegate::RoleLastMsgTime, lastTime);
            m_lastMsgTime[info.id] = lastTime;
        }
        if (isSessionMode && m_chatHistory.contains(info.id) && !m_chatHistory.value(info.id).isEmpty()) {
            const ChatMessage lastMsg = m_chatHistory.value(info.id).last();
            item->setData(ContactDelegate::RoleLastMessagePreview, conversationPreviewForMessage(lastMsg));
            item->setData(ContactDelegate::RoleLastMsgTime, QDateTime::fromSecsSinceEpoch(lastMsg.timestamp));
        } else {
            item->setData(ContactDelegate::RoleLastMessagePreview, QString());
        }

        item->setText(QString::fromUtf8(info.userName));

        ui->contactList->addItem(item);
    }

    for (const auto &info : m_cachedGroupList) {
        m_groupIds.insert(info.groupId);

        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(300, 60));

        item->setData(ContactDelegate::RoleStatus, -info.groupId);
        item->setData(ContactDelegate::RoleName, QString::fromUtf8(info.groupName));
        item->setData(ContactDelegate::RoleIsFriend, true);
        item->setData(ContactDelegate::RoleShowTime, isSessionMode);
        item->setData(ContactDelegate::RoleUnread, 0);
        item->setData(ContactDelegate::RoleOnlineStatus, 1);

        if (info.lastMsgTime > 0) {
            QDateTime lastTime = QDateTime::fromSecsSinceEpoch(info.lastMsgTime);
            item->setData(ContactDelegate::RoleLastMsgTime, lastTime);
            m_groupLastMsgTime[info.groupId] = lastTime;
        }
        if (isSessionMode && m_groupChatHistory.contains(info.groupId) && !m_groupChatHistory.value(info.groupId).isEmpty()) {
            const ChatMessage lastMsg = m_groupChatHistory.value(info.groupId).last();
            item->setData(ContactDelegate::RoleLastMessagePreview, conversationPreviewForMessage(lastMsg));
            item->setData(ContactDelegate::RoleLastMsgTime, QDateTime::fromSecsSinceEpoch(lastMsg.timestamp));
        } else {
            item->setData(ContactDelegate::RoleLastMessagePreview, QString());
        }

        QString displayText = QString("[群聊] %1 (%2人)").arg(QString::fromUtf8(info.groupName)).arg(info.memberCount);
        item->setText(displayText);

        ui->contactList->addItem(item);
    }

    updateEmptyStates();
}

void MainWindow::onFriendListReceived(QList<FriendInfo> list)
{
    for (const auto &info : list) {
        if (!m_friendIds.contains(info.id)) {
            m_sessionVisibleFriendIds.insert(info.id);
        }
    }

    m_cachedFriendList = list;
    refreshContactList();
}

void MainWindow::onContactListClicked(QListWidgetItem *item)
{
    if (!ui->btnChat->isChecked()) {
        return;
    }

    int id = item->data(ContactDelegate::RoleStatus).toInt();
    QString name = item->data(ContactDelegate::RoleName).toString();
    bool isFriend = item->data(ContactDelegate::RoleIsFriend).toBool();
    if (!isFriend) {
        return;
    }

    syncCurrentChatModelToCache();

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
        if (m_groupChatHistory.contains(groupId)) {
            QList<ChatMessage> messages = m_groupChatHistory.value(groupId);
            normalizeMessageList(messages);
            m_chatModel->setMessages(messages);
        }

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
            "QPushButton { background-color: #5865F2; color: white; border-radius: 7px; padding: 0px; font-weight: bold; font-size: 13px; min-width: 64px; max-width: 64px; min-height: 36px; max-height: 36px; }"
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
        updateEmptyStates();
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
        if (m_chatHistory.contains(id)) {
            QList<ChatMessage> messages = m_chatHistory.value(id);
            normalizeMessageList(messages);
            m_chatModel->setMessages(messages);
        }

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
                "QPushButton { background-color: #5865F2; color: white; border-radius: 7px; padding: 0px; font-weight: bold; font-size: 13px; min-width: 64px; max-width: 64px; min-height: 36px; max-height: 36px; }"
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
                "QPushButton { background-color: #40444b; color: #72767d; border-radius: 7px; padding: 0px; font-weight: bold; font-size: 13px; border: none; min-width: 64px; max-width: 64px; min-height: 36px; max-height: 36px; }"
            );
        }
        updateEmptyStates();
    }
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
                appendChatMessage(srcId, msg);
            } else {
                appendChatMessage(srcId, msg, false);
                // 增加未读计数
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int uid = item->data(ContactDelegate::RoleStatus).toInt();
                    if (uid == srcId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        // 触发重绘
                        ui->contactList->viewport()->update();
                        break;
                    }
                }
            }
            return;
        }
        
        QString text = QString::fromUtf8(realData);

        ChatMessage msg(text, false, ":/res/you.jpeg"); // 对方头像
        if (m_currentFriendId == srcId) {
            appendChatMessage(srcId, msg);
        } else {
            // 否则应该显示红点提示
            appendChatMessage(srcId, msg, false);
            // 找到好友列表里对应的 Item，增加未读计数
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int uid = item->data(ContactDelegate::RoleStatus).toInt();
                if (uid == srcId) {
                    // 取出当前未读数 (RoleUnread)
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    // 加 1
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    // 触发重绘
                    ui->contactList->viewport()->update();
                    break;
                }
            }
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
                appendChatMessage(srcId, msg);
            } else {
                appendChatMessage(srcId, msg, false);
                // 增加未读计数
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int uid = item->data(ContactDelegate::RoleStatus).toInt();
                    if (uid == srcId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        // 触发重绘
                        ui->contactList->viewport()->update();
                        break;
                    }
                }
            }
            return;
        }
        
        ChatMessage msg(realData, false, ":/res/you.jpeg");
        if (m_currentFriendId == srcId) {
            appendChatMessage(srcId, msg);
        }else{
            // 红点显示
            appendChatMessage(srcId, msg, false);
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int uid = item->data(ContactDelegate::RoleStatus).toInt();
                if (uid == srcId) {
                    // 取出当前未读数 (RoleUnread)
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    // 加 1
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    // 触发重绘
                    ui->contactList->viewport()->update();
                    break;
                }
            }
        }
        
        // 更新最后消息时间
        updateContactLastMsgTime(srcId, QDateTime::currentDateTime());
    }
}

void MainWindow::onSigChatHistoryReceived(int friendId, const QList<std::tuple<int, QByteArray, quint64>> &history)
{
    if (m_isGroupChat || friendId != m_currentFriendId) return;

    QList<ChatMessage> localMessages;
    const auto cachedMessages = m_chatHistory.value(friendId);
    for (const ChatMessage &cachedMsg : cachedMessages) {
        localMessages.append(cachedMsg);
    }

    QList<ChatMessage> mergedMessages;

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
            mergedMessages.append(msg);

        } else {
            // --- 文本处理 ---
            QString text = QString::fromUtf8(content);
            ChatMessage msg(text, isMe, avatar, timestamp);
            mergedMessages.append(msg);
        }
    }

    for (const ChatMessage &localMsg : localMessages) {
        if (localMsg.type == TypeFile) {
            mergedMessages.append(localMsg);
            continue;
        }

        bool exists = false;
        for (const ChatMessage &serverMsg : mergedMessages) {
            if (serverMsg.type == localMsg.type
                && serverMsg.timestamp == localMsg.timestamp
                && serverMsg.isMine == localMsg.isMine
                && serverMsg.content == localMsg.content
                && serverMsg.image == localMsg.image) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            mergedMessages.append(localMsg);
        }
    }

    normalizeMessageList(mergedMessages);
    m_chatHistory[friendId] = mergedMessages;
    m_chatModel->setMessages(mergedMessages);
    if (!mergedMessages.isEmpty()) {
        const ChatMessage &lastMessage = mergedMessages.last();
        updateContactLastMessagePreview(friendId,
                                        conversationPreviewForMessage(lastMessage),
                                        QDateTime::fromSecsSinceEpoch(lastMessage.timestamp));
    }
    ui->chatList->scrollToBottom();
    updateEmptyStates();
}

void MainWindow::onSigFriendStatusChanged(int uid, int status)
{
    for (int i = 0; i < ui->contactList->count(); ++i) {
        QListWidgetItem* item = ui->contactList->item(i);

        int itemUid = item->data(ContactDelegate::RoleStatus).toInt();

        if (itemUid == uid) {
            item->setData(ContactDelegate::RoleOnlineStatus, status);
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
        m_lastSearchKeyword.clear();
        // 搜索框清空后再次显示回好友列表
        NetworkManager::instance().sendMsg(MSG_FRIEND_LIST_REQ,QByteArray());
    }else if(keyword == m_lastSearchKeyword){
        return;
    }else{
        m_lastSearchKeyword = keyword;
        m_activeSearchRequestId = qHash(keyword) ^ static_cast<quint32>(m_currentUserId);
        // 发送搜索请求
        SearchReq req;
        memset(&req,0,sizeof(SearchReq));
        strncpy(req.keyword,keyword.toStdString().c_str(),32);

        NetworkManager::instance().sendMsg(MSG_SEARCH_USER_REQ,QByteArray((char*)&req,sizeof(SearchReq)));
    }
}

void MainWindow::onSigSearchUserResult(QList<FriendInfo> list, quint32 requestId, bool reset, bool hasMore)
{
    if (requestId != 0 && requestId != m_activeSearchRequestId) {
        return;
    }

    if (reset) {
        ui->contactList->clear();
    }

    for(const auto &info : list){
        QListWidgetItem *item = new QListWidgetItem(ui->contactList);
        item->setSizeHint(QSize(300,60));

        // 判断是否是当前用户好友（排除自己）
        bool isFriend = m_friendIds.contains(info.id) || info.id == m_currentUserId;
        const QString userName = QString::fromUtf8(info.userName);

        // item中保存用户id，用户名，是否与当前用户是好友关系
        item->setData(ContactDelegate::RoleStatus,info.id);
        item->setData(ContactDelegate::RoleName,userName);
        item->setData(ContactDelegate::RoleIsFriend,isFriend);
        item->setData(ContactDelegate::RoleUnread, 0); // 初始化未读计数为0
        item->setData(ContactDelegate::RoleOnlineStatus, info.status);

        ui->contactList->addItem(item);

        if (!isFriend) {
            item->setText("");
            item->setData(ContactDelegate::RoleName, "");

            QWidget *rowWidget = new QWidget(ui->contactList);
            rowWidget->setAttribute(Qt::WA_StyledBackground, true);
            rowWidget->setStyleSheet(R"(
                QWidget {
                    background-color: transparent;
                }
                QLabel {
                    color: #8e9297;
                    font-family: "Microsoft YaHei";
                    font-size: 14px;
                }
                QPushButton {
                    background-color: #5865f2;
                    color: white;
                    border: none;
                    border-radius: 4px;
                    font-family: "Microsoft YaHei";
                    font-size: 12px;
                    padding: 2px 10px;
                    min-width: 68px;
                    max-width: 68px;
                    min-height: 24px;
                    max-height: 24px;
                }
                QPushButton:hover {
                    background-color: #6772f4;
                }
                QPushButton:pressed {
                    background-color: #4752c4;
                }
                QPushButton:disabled {
                    background-color: #444b62;
                    color: #9aa3b8;
                }
            )");

            QHBoxLayout *layout = new QHBoxLayout(rowWidget);
            layout->setContentsMargins(22, 0, 36, 0);
            layout->setSpacing(12);

            QLabel *nameLabel = new QLabel(userName, rowWidget);
            nameLabel->setTextInteractionFlags(Qt::NoTextInteraction);

            QPushButton *addButton = new QPushButton("添加好友", rowWidget);
            addButton->setCursor(Qt::PointingHandCursor);
            addButton->setFocusPolicy(Qt::NoFocus);

            layout->addWidget(nameLabel, 1);
            layout->addWidget(addButton, 0, Qt::AlignVCenter);

            connect(addButton, &QPushButton::clicked, this, [this, targetId = info.id, addButton]() {
                if (targetId == m_currentUserId) {
                    return;
                }

                AddFriendReq req;
                req.targetId = targetId;
                NetworkManager::instance().sendMsg(MSG_ADD_FRIEND_REQ, QByteArray((char*)&req, sizeof(AddFriendReq)));

                addButton->setEnabled(false);
                addButton->setText("已发送");
                QMessageBox::information(this, "提示", "好友请求已发送");
            });

            ui->contactList->setItemWidget(item, rowWidget);
        } else {
            item->setText(userName);
        }
    }

    Q_UNUSED(hasMore)
    updateEmptyStates();
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

    updateEmptyStates();
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

        const QString detail = isResume ? QString("继续接收 · %1").arg(sizeStr) : QString("接收中 · 0% · %1").arg(sizeStr);
        ChatMessage fileMsg = makeFileMessage(fileId, fileName, detail, false);
        fileMsg.filePeerId = senderId;
        fileMsg.fileProgress = isResume && state.totalChunks > 0 ? (state.completedChunks.size() * 100) / state.totalChunks : 0;
        fileMsg.fileTotalBytes = fileSize;
        appendChatMessage(senderId, fileMsg);
    }

    QByteArray packet = makePacket(MSG_FILE_TRANSFER_RESP,QByteArray((char*)&resp,sizeof(FileTransferResp)),0,senderId);
    NetworkManager::instance().sendRow(packet);
}

void MainWindow::onsigFileTransferResponse(const QString &fileId, bool accepted)
{
    if (accepted) {
        if(m_pendingFileTransfers.contains(fileId)){
            QString filePath = m_pendingFileTransfers.take(fileId);
            int targetFriendId = m_pendingFileTransferTargets.take(fileId);
            qint64 declaredFileSize = m_pendingFileTransferSizes.take(fileId);
            if (targetFriendId <= 0) {
                targetFriendId = m_currentFriendId;
            }

            TransferState state = TransferStateManager::instance().loadTransferState(fileId);
            if(!state.fileId.isEmpty() && state.completedChunks.size() > 0){
                LOG_INFO_FMT("恢复文件传输 %1（从第 %2 个分片开始）",state.fileName,state.completedChunks.size());
            }

            QFileInfo fileInfo(filePath);
            if (declaredFileSize <= 0) {
                declaredFileSize = fileInfo.size();
            }
            if (!fileInfo.exists() || fileInfo.size() < declaredFileSize) {
                QMessageBox::warning(this, "传输失败", "文件大小已变化，请重新选择文件");
                return;
            }
            const QString fileName = fileInfo.fileName();
            QString sizeStr;
            if (declaredFileSize < 1024) {
                sizeStr = QString::number(declaredFileSize) + "B";
            } else if (declaredFileSize < 1024 * 1024) {
                sizeStr = QString::number(declaredFileSize / 1024.0, 'f', 2) + "KB";
            } else {
                sizeStr = QString::number(declaredFileSize / (1024.0 * 1024.0), 'f', 2) + "MB";
            }
            const bool isResume = !state.fileId.isEmpty() && state.completedChunks.size() > 0;
            const QString detail = isResume
                ? QString("继续发送 · %1 · 已完成 %2/%3 分片").arg(sizeStr).arg(state.completedChunks.size()).arg(state.totalChunks)
                : QString("发送中 · %1").arg(sizeStr);
            ChatMessage fileMsg = makeFileMessage(fileId, fileName, detail, true);
            fileMsg.filePath = filePath;
            fileMsg.filePeerId = targetFriendId;
            fileMsg.fileProgress = isResume && state.totalChunks > 0 ? (state.completedChunks.size() * 100) / state.totalChunks : 0;
            fileMsg.fileTransferredBytes = isResume && state.totalChunks > 0
                ? qMin<qint64>(declaredFileSize, (qint64)state.completedChunks.size() * FILE_TRANSFER_CHUNK_SIZE)
                : 0;
            fileMsg.fileTotalBytes = declaredFileSize;
            appendChatMessage(targetFriendId, fileMsg);

            const bool notInTargetChat = (m_currentFriendId != targetFriendId) || m_isGroupChat;
            if (notInTargetChat) {
                QMessageBox::information(
                    this,
                    "文件传输提示",
                    QString("好友 %1 已同意接收文件，传输将在后台开始。").arg(targetFriendId));
            }

            FileTransferManager::instance().startSendFile(fileId, filePath, targetFriendId, declaredFileSize);
        }else{
            LOG_WARN_FMT("File path not found for fileId:%1",fileId);
        }
    } else {
        m_pendingFileTransfers.remove(fileId);
        m_pendingFileTransferTargets.remove(fileId);
        m_pendingFileTransferSizes.remove(fileId);
        QMessageBox::warning(this, "被拒绝", "对方拒绝接收文件");
    }
}

void MainWindow::onFileTransferStarted(const QString &fileId, const QString &fileName)
{
    Q_UNUSED(fileId)
    Q_UNUSED(fileName)
}

void MainWindow::onFileTransferProgress(const QString &fileId, int percent, qint64 sent, qint64 total)
{
    updateFileMessageProgress(fileId, formatTransferDetail("发送中", percent, sent, total), percent, sent, total);
}

void MainWindow::onFileTransferCompleted(const QString &fileId)
{
    updateFileMessageProgress(fileId, "已发送 · 100%", 100);

    QMessageBox::information(this, "文件传输", "文件传输完成！");
    LOG_INFO_FMT("File transfer completed:%1",fileId);
}

void MainWindow::onFileTransferFailed(const QString &fileId, const QString &error)
{
    updateFileMessageProgress(fileId, "发送失败", -1);

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

    QByteArray packet = makePacketFromParts(MSG_FILE_CHUNK,
                                            reinterpret_cast<const char*>(&chunkHeader),
                                            sizeof(FileChunk),
                                            chunk,
                                            0,
                                            friendId);
    NetworkManager::instance().sendFilePacket(packet);

    Q_UNUSED(totalChunks)
}

void MainWindow::onFileReceiveChunk(const QString &fileId, int chunkIndex, const QByteArray &chunk, int senderId)
{
    if (!FileReceiver::instance().receiveChunk(fileId,chunkIndex,chunk)) {
        return;
    }

    queueFileTransferAck(fileId, chunkIndex, senderId);
}

void MainWindow::onFileTransferAck(const QString &fileId, int chunkIndex, int receiverId)
{
    Q_UNUSED(receiverId)
    FileTransferManager::instance().onChunkAcked(fileId, chunkIndex);
}

void MainWindow::onFileTransferAckBatch(const QString &fileId, const QList<int> &chunkIndexes, int receiverId)
{
    Q_UNUSED(receiverId)
    FileTransferManager::instance().onChunksAcked(fileId, chunkIndexes);
}

void MainWindow::queueFileTransferAck(const QString &fileId, int chunkIndex, int senderId)
{
    QList<int> &batch = m_pendingFileAckBatches[fileId];
    batch.append(chunkIndex);
    m_pendingFileAckSenders[fileId] = senderId;

    if (batch.size() >= FILE_TRANSFER_ACK_BATCH_SIZE) {
        sendFileTransferAckBatch(fileId, batch, senderId);
        m_pendingFileAckBatches.remove(fileId);
        m_pendingFileAckSenders.remove(fileId);
        return;
    }

    if (!m_fileAckFlushTimer) {
        m_fileAckFlushTimer = new QTimer(this);
        m_fileAckFlushTimer->setSingleShot(true);
        m_fileAckFlushTimer->setInterval(20);
        connect(m_fileAckFlushTimer, &QTimer::timeout, this, &MainWindow::flushFileTransferAcks);
    }

    if (!m_fileAckFlushTimer->isActive()) {
        m_fileAckFlushTimer->start();
    }
}

void MainWindow::flushFileTransferAcks()
{
    const QStringList fileIds = m_pendingFileAckBatches.keys();
    for (const QString &fileId : fileIds) {
        const QList<int> chunkIndexes = m_pendingFileAckBatches.take(fileId);
        const int senderId = m_pendingFileAckSenders.take(fileId);
        if (!chunkIndexes.isEmpty()) {
            sendFileTransferAckBatch(fileId, chunkIndexes, senderId);
        }
    }
}

void MainWindow::sendFileTransferAckBatch(const QString &fileId, const QList<int> &chunkIndexes, int senderId)
{
    if (chunkIndexes.isEmpty()) {
        return;
    }

    FileTransferAckBatchHeader header;
    memset(&header, 0, sizeof(header));
    strncpy(header.latestAck.fileId, fileId.toUtf8().constData(), sizeof(header.latestAck.fileId) - 1);
    header.latestAck.chunkIndex = static_cast<quint32>(chunkIndexes.last());
    header.ackCount = static_cast<quint32>(chunkIndexes.size());

    QByteArray body(reinterpret_cast<const char*>(&header), sizeof(header));
    for (int chunkIndex : chunkIndexes) {
        const quint32 ackedChunk = static_cast<quint32>(chunkIndex);
        body.append(reinterpret_cast<const char*>(&ackedChunk), sizeof(ackedChunk));
    }

    NetworkManager::instance().sendRow(makePacket(MSG_FILE_TRANSFER_ACK, body, 0, senderId));
}

void MainWindow::onFileReceiveProgress(const QString &fileId, int percent, qint64 received, qint64 total)
{
    updateFileMessageProgress(fileId, formatTransferDetail("接收中", percent, received, total), percent, received, total);
}

void MainWindow::onFileReceiveCompleted(const QString &fileId, const QString &savePath)
{
    LOG_INFO_FMT("File receive completed:%1",savePath);
    updateFileMessageProgress(fileId, "已接收 · 100%", 100, 0, 0, savePath);
    
    // 使用QTimer::singleShot延迟显示对话框，避免在信号处理过程中阻塞
    // 这对于防止程序崩溃非常重要，特别是当信号从非主线程发射时
    QTimer::singleShot(100, this, [this, fileId, savePath]() {
        try {
            QMessageBox::information(this, "文件接收完成",
                                     QString("文件已保存到:\n%1").arg(savePath));
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in file receive completed dialog: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in file receive completed dialog");
        }
    });
}

void MainWindow::onFileReceiveFailed(const QString &fileId, const QString &error)
{
    LOG_ERROR_FMT("File %1 received failed,%2",fileId,error);
    updateFileMessageProgress(fileId, "接收失败", -1);
    
    // 使用QTimer::singleShot延迟显示对话框，避免在信号处理过程中阻塞
    QTimer::singleShot(100, this, [this, fileId, error]() {
        QMessageBox::information(this, "接收失败", "文件接收失败: " + error);
    });
}

void MainWindow::onGroupListReceived(QList<GroupInfo> list)
{
    m_cachedGroupList = list;
    refreshContactList();
}

void MainWindow::onGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body, quint64 messageId)
{
    if (body.isEmpty()) return;

    char subType = body[0];
    QByteArray realData = body.mid(1);

    bool isMe = (senderId == m_currentUserId);
    QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";
    const QString displaySenderName = senderName.trimmed().isEmpty()
        ? (isMe ? QString::fromUtf8("\xE6\x88\x91")
                : QString::fromUtf8("\xE6\x9C\xAA\xE7\x9F\xA5\xE7\x94\xA8\xE6\x88\xB7"))
        : senderName;

    if (subType == SUB_TEXT) {
        // 检查解密是否失败（内容为空）
        if (realData.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received group message with empty content from group %1 - decryption may have failed", groupId);
            QString errorText = "[群消息解密失败]";
            ChatMessage msg(errorText, isMe, avatar, displaySenderName);
            
            if (m_isGroupChat && m_currentGroupId == groupId) {
                appendChatMessage(-groupId, msg);
                ui->chatList->scrollToBottom();
                if (messageId > 0) {
                    GroupMsgCursorAck ack;
                    ack.groupId = groupId;
                    ack.messageId = messageId;
                    NetworkManager::instance().sendMsg(MSG_GROUP_MSG_READ_ACK, QByteArray((char*)&ack, sizeof(ack)));
                }
            } else {
                appendChatMessage(-groupId, msg, false);
                // 显示未读红点
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int itemId = item->data(ContactDelegate::RoleStatus).toInt();
                    if (itemId < 0 && -itemId == groupId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        // 触发重绘
                        ui->contactList->viewport()->update();
                        break;
                    }
                }
            }
            return;
        }
        
        QString text = QString::fromUtf8(realData);
        ChatMessage msg(text, isMe, avatar, displaySenderName);

        // 如果当前正在这个群聊
        if (m_isGroupChat && m_currentGroupId == groupId) {
            appendChatMessage(-groupId, msg);
            ui->chatList->scrollToBottom();
            if (messageId > 0) {
                GroupMsgCursorAck ack;
                ack.groupId = groupId;
                ack.messageId = messageId;
                NetworkManager::instance().sendMsg(MSG_GROUP_MSG_READ_ACK, QByteArray((char*)&ack, sizeof(ack)));
            }
        } else {
            // 存入历史记录
            appendChatMessage(-groupId, msg, false);

            // 显示未读红点：找到群聊列表项并增加未读计数
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int itemId = item->data(ContactDelegate::RoleStatus).toInt();

                // 负数ID表示群聊
                if (itemId < 0 && -itemId == groupId) {
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    // 触发重绘
                    ui->contactList->viewport()->update();
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
            ChatMessage msg(errorText, isMe, avatar, displaySenderName);
            
            if (m_isGroupChat && m_currentGroupId == groupId) {
                appendChatMessage(-groupId, msg);
                ui->chatList->scrollToBottom();
                if (messageId > 0) {
                    GroupMsgCursorAck ack;
                    ack.groupId = groupId;
                    ack.messageId = messageId;
                    NetworkManager::instance().sendMsg(MSG_GROUP_MSG_READ_ACK, QByteArray((char*)&ack, sizeof(ack)));
                }
            } else {
                appendChatMessage(-groupId, msg, false);
                // 显示未读红点
                for (int i = 0; i < ui->contactList->count(); ++i) {
                    QListWidgetItem *item = ui->contactList->item(i);
                    int itemId = item->data(ContactDelegate::RoleStatus).toInt();
                    if (itemId < 0 && -itemId == groupId) {
                        int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                        item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                        // 触发重绘
                        ui->contactList->viewport()->update();
                        break;
                    }
                }
            }
            return;
        }
        
        ChatMessage msg(realData, isMe, avatar, displaySenderName);

        if (m_isGroupChat && m_currentGroupId == groupId) {
            appendChatMessage(-groupId, msg);
            ui->chatList->scrollToBottom();
            if (messageId > 0) {
                GroupMsgCursorAck ack;
                ack.groupId = groupId;
                ack.messageId = messageId;
                NetworkManager::instance().sendMsg(MSG_GROUP_MSG_READ_ACK, QByteArray((char*)&ack, sizeof(ack)));
            }
        } else {
            appendChatMessage(-groupId, msg, false);

            // 显示未读红点
            for (int i = 0; i < ui->contactList->count(); ++i) {
                QListWidgetItem *item = ui->contactList->item(i);
                int itemId = item->data(ContactDelegate::RoleStatus).toInt();

                if (itemId < 0 && -itemId == groupId) {
                    int currentCount = item->data(ContactDelegate::RoleUnread).toInt();
                    item->setData(ContactDelegate::RoleUnread, currentCount + 1);
                    // 触发重绘
                    ui->contactList->viewport()->update();
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

    QList<ChatMessage> mergedMessages = m_groupChatHistory.value(groupId);

    for (const auto &item : history) {
        int senderId = std::get<0>(item);
        QString senderName = std::get<1>(item);
        QByteArray rawBody = std::get<2>(item);
        quint64 timestamp = std::get<3>(item);

        if (rawBody.isEmpty()) continue;

        bool isMe = (senderId == m_currentUserId);
        QString avatar = isMe ? ":/res/me.jpg" : ":/res/you.jpeg";
        const QString displaySenderName = senderName.trimmed().isEmpty()
            ? (isMe ? QString::fromUtf8("\xE6\x88\x91")
                    : QString::fromUtf8("\xE6\x9C\xAA\xE7\x9F\xA5\xE7\x94\xA8\xE6\x88\xB7"))
            : senderName;

        char msgType = rawBody[0];
        QByteArray realContent = rawBody.mid(1);
        
        // 检查解密是否失败（内容为空）
        if (realContent.isEmpty()) {
            LOG_WARN_FMT("[MainWindow] Received group history with empty content from group %1 - decryption may have failed", groupId);
            if (msgType == SUB_IMAGE) {
                ChatMessage msg(QString("[历史群图片解密失败]"), isMe, avatar, displaySenderName, timestamp);
                mergedMessages.append(msg);
            } else {
                ChatMessage msg(QString("[历史群消息解密失败]"), isMe, avatar, displaySenderName, timestamp);
                mergedMessages.append(msg);
            }
            continue;
        }

        if (msgType == SUB_IMAGE) {
            ChatMessage msg(realContent, isMe, avatar, displaySenderName, timestamp);
            mergedMessages.append(msg);
        } else {
            QString text = QString::fromUtf8(realContent);
            ChatMessage msg(text, isMe, avatar, displaySenderName, timestamp);
            mergedMessages.append(msg);
        }
    }
    normalizeMessageList(mergedMessages);
    m_groupChatHistory[groupId] = mergedMessages;
    m_chatModel->setMessages(mergedMessages);
    if (!mergedMessages.isEmpty()) {
        const ChatMessage &lastMessage = mergedMessages.last();
        updateContactLastMessagePreview(-groupId,
                                        conversationPreviewForMessage(lastMessage),
                                        QDateTime::fromSecsSinceEpoch(lastMessage.timestamp));
    }
    ui->chatList->scrollToBottom();
    saveFileMessageHistory();
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

        const QString detail = QString("请求恢复 · 已完成 %1/%2 分片")
                                   .arg(state.completedChunks.size())
                                   .arg(state.totalChunks);
        appendChatMessage(friendId, makeFileMessage(fileId, state.fileName, detail, true));
    }else{
        // 恢复接收
        // 接收方只需等待对方继续发送，FileReceiver 会自动处理断点续传
        LOG_INFO_FMT("等待对方继续发送文件: %1", state.fileName);

        const QString detail = "等待恢复";
        appendChatMessage(friendId, makeFileMessage(fileId, state.fileName, detail, false));
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

        const QString detail = QString("继续发送 · 已完成 %1/%2 分片").arg(receivedChunks).arg(totalChunks);
        appendChatMessage(state.friendId, makeFileMessage(fileId, state.fileName, detail, true));

        // 开始传输（FileTransferManager会自动跳过已完成的分片）
        FileTransferManager::instance().startSendFile(fileId, state.filePath, state.friendId, state.fileSize);
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

void MainWindow::onFileTransferCanceled(const QString &fileId, int senderId, int reason)
{
    Q_UNUSED(reason)

    FileReceiver::instance().cancelReceiving(fileId);
    ReconnectTransferManager::instance().removeCompletedTransfer(fileId);

    LOG_INFO(QString("File transfer canceled by peer: fileId=%1 sender=%2").arg(fileId).arg(senderId));
    QTimer::singleShot(100, this, [this, fileId]() {
        QMessageBox::information(this, "文件传输", QString("对方已取消文件传输：%1").arg(fileId));
    });
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
        const QString currentSenderName = m_currentUserName.trimmed().isEmpty()
            ? QString::fromUtf8("\xE6\x88\x91")
            : m_currentUserName;
        strncpy(header.senderName, currentSenderName.toUtf8().constData(), 31);
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
        ChatMessage msg(text, true, ":/res/me.jpg", currentSenderName);
        appendChatMessage(-m_currentGroupId, msg);
        
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
        appendChatMessage(m_currentFriendId, msg);
        
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
    loadFileMessageHistory();
    
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
    updateEmptyStates();
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
    updateEmptyStates();
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
    updateEmptyStates();
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
        const QString currentSenderName = m_currentUserName.trimmed().isEmpty()
            ? QString::fromUtf8("\xE6\x88\x91")
            : m_currentUserName;
        strncpy(header.senderName, currentSenderName.toUtf8().constData(), 31);
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
        ChatMessage msg(imageData, true, ":/res/me.jpg", currentSenderName);
        appendChatMessage(-m_currentGroupId, msg);
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
        appendChatMessage(m_currentFriendId, msg);
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
    m_pendingFileTransferTargets[fileId] = m_currentFriendId;
    m_pendingFileTransferSizes[fileId] = fileInfo.size();

    // 先发送文件传输请求给对方
    FileTransferReq req;
    memset(&req,0,sizeof(FileTransferReq));

    // 文件名和文件大小
    strncpy(req.fileName,fileInfo.fileName().toUtf8().constData(),255);
    req.fileName[255] = '\0';
    req.fileSize = fileInfo.size();

    // 分片总数
    quint64 chunkSize = FILE_TRANSFER_CHUNK_SIZE;
    req.totalChunks = (fileInfo.size() + chunkSize - 1) / chunkSize;

    strncpy(req.fileId,fileId.toUtf8().constData(),63);
    req.fileId[63] = '\0';

    // 发送请求
    QByteArray packet = makePacket(MSG_FILE_TRANSFER_REQ,QByteArray((char*)&req,sizeof(FileTransferReq)),0,m_currentFriendId);
    NetworkManager::instance().sendRow(packet);

    ChatMessage pendingMsg = makeFileMessage(fileId, fileInfo.fileName(), "等待对方接收 · 0%", true);
    pendingMsg.filePath = filePath;
    pendingMsg.filePeerId = m_currentFriendId;
    pendingMsg.fileProgress = 0;
    pendingMsg.fileTotalBytes = fileInfo.size();
    appendChatMessage(m_currentFriendId, pendingMsg);
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

void MainWindow::onChatListContextMenu(const QPoint &pos)
{
    const QModelIndex index = ui->chatList->indexAt(pos);
    ChatMessage msg = currentFileMessageAt(index);
    if (msg.type != TypeFile || msg.fileId.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction *openFileAction = menu.addAction("打开文件");
    QAction *openFolderAction = menu.addAction("打开所在文件夹");
    menu.addSeparator();
    QAction *retrySendAction = menu.addAction("重新发送");
    QAction *retryReceiveAction = menu.addAction("重新接收");
    QAction *cancelAction = menu.addAction("取消传输");

    const QString openPath = fileMessageOpenPath(msg);
    openFileAction->setEnabled(!openPath.isEmpty() && QFileInfo::exists(openPath));
    openFolderAction->setEnabled(!openPath.isEmpty() && QFileInfo(openPath).absoluteDir().exists());
    retrySendAction->setEnabled(msg.isMine);
    retryReceiveAction->setEnabled(!msg.isMine);

    QAction *selected = menu.exec(ui->chatList->viewport()->mapToGlobal(pos));
    if (!selected) {
        return;
    }

    int chatId = 0;
    if (ChatMessage *stored = findFileMessage(msg.fileId, &chatId)) {
        msg = *stored;
    }

    if (selected == openFileAction) {
        const QString path = fileMessageOpenPath(msg);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::information(this, "打开文件", "本地文件不存在或尚未接收完成");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    if (selected == openFolderAction) {
        const QString path = fileMessageOpenPath(msg);
        QFileInfo info(path);
        if (path.isEmpty() || !info.absoluteDir().exists()) {
            QMessageBox::information(this, "打开文件夹", "本地文件夹不存在");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
        return;
    }

    if (selected == retrySendAction) {
        const QString path = msg.filePath.isEmpty() ? fileMessageOpenPath(msg) : msg.filePath;
        QFileInfo info(path);
        const int peerId = msg.filePeerId > 0 ? msg.filePeerId : (chatId > 0 ? chatId : m_currentFriendId);
        if (!info.exists() || !info.isFile() || peerId <= 0) {
            QMessageBox::warning(this, "重新发送", "找不到原文件或接收方信息，无法重新发送");
            return;
        }

        const QString newFileId = FileTransferManager::instance().generateFileId(path);
        m_pendingFileTransfers[newFileId] = path;
        m_pendingFileTransferTargets[newFileId] = peerId;
        m_pendingFileTransferSizes[newFileId] = info.size();

        FileTransferReq req;
        memset(&req, 0, sizeof(FileTransferReq));
        strncpy(req.fileName, info.fileName().toUtf8().constData(), sizeof(req.fileName) - 1);
        req.fileSize = info.size();
        req.totalChunks = (info.size() + FILE_TRANSFER_CHUNK_SIZE - 1) / FILE_TRANSFER_CHUNK_SIZE;
        strncpy(req.fileId, newFileId.toUtf8().constData(), sizeof(req.fileId) - 1);
        QByteArray packet = makePacket(MSG_FILE_TRANSFER_REQ, QByteArray((char*)&req, sizeof(FileTransferReq)), 0, peerId);
        NetworkManager::instance().sendRow(packet);

        ChatMessage resendMsg = makeFileMessage(newFileId, info.fileName(), "等待对方接收 · 0%", true);
        resendMsg.filePath = path;
        resendMsg.filePeerId = peerId;
        resendMsg.fileProgress = 0;
        resendMsg.fileTotalBytes = info.size();
        appendChatMessage(peerId, resendMsg);
        return;
    }

    if (selected == retryReceiveAction) {
        const int peerId = msg.filePeerId > 0 ? msg.filePeerId : (chatId > 0 ? chatId : m_currentFriendId);
        if (peerId <= 0) {
            QMessageBox::warning(this, "重新接收", "找不到发送方信息，无法重新接收");
            return;
        }
        NetworkManager::instance().requestResumeTransfer(msg.fileId, peerId);
        updateFileMessageProgress(msg.fileId, "请求重新接收 · 0%", 0);
        return;
    }

    if (selected == cancelAction) {
        const int peerId = msg.filePeerId > 0 ? msg.filePeerId : (chatId > 0 ? chatId : m_currentFriendId);
        FileTransferManager::instance().cancelTransfer(msg.fileId);
        FileReceiver::instance().cancelReceiving(msg.fileId);
        ReconnectTransferManager::instance().removeCompletedTransfer(msg.fileId);
        TransferStateManager::instance().removeTransferState(msg.fileId);
        if (peerId > 0) {
            NetworkManager::instance().requestCancelTransfer(msg.fileId, peerId);
        }
        updateFileMessageProgress(msg.fileId, "已取消", -1);
    }
}

void MainWindow::onChatListDoubleClicked(const QModelIndex &index)
{
    ChatMessage msg = currentFileMessageAt(index);
    if (msg.type != TypeFile || msg.fileId.isEmpty()) {
        return;
    }

    int chatId = 0;
    if (ChatMessage *stored = findFileMessage(msg.fileId, &chatId)) {
        Q_UNUSED(chatId)
        msg = *stored;
    }

    const QString path = fileMessageOpenPath(msg);
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
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

