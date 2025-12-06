#include "groupdialog.h"
#include "ui_groupdialog.h"

#include <QMessageBox>
#include <QMouseEvent>

GroupDialog::GroupDialog(const QList<FriendSelectInfo>& friends,QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::GroupDialog)
{
    ui->setupUi(this);
    this->m_friends = friends;

    // 去掉系统边框，设置为模态
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);

    // 加载好友列表
    for(const auto& f : m_friends) {
        QListWidgetItem* item = new QListWidgetItem(ui->listWidget);
        item->setText(f.name);
        item->setData(Qt::UserRole, f.id);

        // 设置为复选框模式
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);

        ui->listWidget->addItem(item);
    }

    connect(ui->listWidget,&QListWidget::itemChanged,this,&GroupDialog::updateSelectionCount);

    // 监听输入框文本变化（查找用户输入框）
    connect(ui->editUserName, &QLineEdit::textChanged, this, &GroupDialog::onSearchTextChanged);

    initUI();
}

GroupDialog::~GroupDialog()
{
    delete ui;
}

void GroupDialog::mousePressEvent(QMouseEvent *event)
{
    // 只有左键点击才能拖动
    if (event->button() == Qt::LeftButton) {
        // 计算鼠标相对于窗口左上角的偏移量
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void GroupDialog::mouseMoveEvent(QMouseEvent *event)
{
    // 只有按住左键移动才处理
    if (event->buttons() & Qt::LeftButton) {
        // 移动窗口到鼠标当前位置减去偏移量
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void GroupDialog::updateSelectionCount()
{
    int count = 0;
    for(int i = 0; i < ui->listWidget->count(); ++i){
        QListWidgetItem *item = ui->listWidget->item(i);
        if(item->checkState() == Qt::Checked){
            count++;
            m_selectFriendIds.append(item->data(Qt::UserRole).toInt());
        }
    }

    ui->lbSelect->setText(QString("已选择%1人").arg(count));
}

void GroupDialog::onSearchTextChanged(const QString &text)
{
    QString keyword = text.trimmed().toLower();

    for (int i = 0; i < ui->listWidget->count(); ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        QString friendName = item->text().toLower();

        // 如果关键字为空，显示所有项
        // 否则只显示包含关键字的项
        if (keyword.isEmpty() || friendName.contains(keyword)) {
            item->setHidden(false);
        } else {
            item->setHidden(true);
        }
    }
}

void GroupDialog::initUI()
{

    // 应用样式
    QString style = R"(
        QWidget {
            background-color: #36393f;
            border: 1px solid #202225;
            border-radius: 10px;
        }

        QPushButton#btnClose {
            background: transparent;
            color: #dcddde;
            border: none;
            font-size: 16px;
            font-weight: bold;
            border-radius: 4px;
        }
        QPushButton#btnClose:hover {
            background-color: #FA3C3C;
            color: white;
        }
        QLabel {
            color: #dcddde; font-family: "Microsoft YaHei"; border:none;
        }

        QLabel#lbTitle{
            font-size: 16px;
        }

        QLabel#lbGroupMsg{
            font-size: 12px;
        }

        QLabel#lbSelect{
            font-size: 10px;
        }

        QLineEdit {
            background-color: #202225; color: white; border: none; border-radius: 4px; padding: 8px;
        }

        QListWidget {
            background-color: #2f3136; border-radius: 4px; color: #dcddde; outline: none;
        }

        QListWidget::item {
            padding: 8px;
        }

        QListWidget::item:hover {
            background-color: #35373c;
        }

        /* 复选框样式自定义 (可选) */
        QCheckBox {
            color: #dcddde;
        }


        QPushButton#btnCreate {
            background-color: #5865F2;
            color: white;
            border-radius: 4px;
            border: none; padding: 8px 16px;
            font-weight: bold;
        }

        QPushButton#btnCreate:hover {
            background-color: #4752c4;
        }

        QPushButton#btnCancel {
            color: #dcddde;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }

        QPushButton#btnCancel:hover {
            background-color: #40444b; /* 悬浮背景变深 */
            color: white;
        }
    )";
    this->setStyleSheet(style);
    updateSelectionCount();
}

QString GroupDialog::getGroupName() const
{
    return ui->editGroupName->text().trimmed();
}

QList<int> GroupDialog::getSelctFriendIds() const
{
    return m_selectFriendIds;
}

void GroupDialog::on_btnCancel_clicked()
{
    this->reject();
}

void GroupDialog::on_btnClose_clicked()
{
    this->reject();
}


void GroupDialog::on_btnCreate_clicked()
{
    if(getGroupName().isEmpty()){
        QMessageBox::warning(this, "提示", "请输入群聊名称");
        return;
    }

    this->accept();
}

