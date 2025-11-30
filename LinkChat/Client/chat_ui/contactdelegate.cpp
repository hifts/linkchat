#include "contactdelegate.h"

#include <QPainter>

ContactDelegate::ContactDelegate(QObject *parent)
    : QStyledItemDelegate{parent}
{}

void ContactDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 1. 获取数据
    QString name = index.data(RoleName).toString();
    int status = index.data(RoleStatus).toInt(); // 这里的Status其实你存的是ID，状态可能是通过其他方式判断，或者你需要额外存一个RoleStatusInt

    // 获取未读数 (我们在 MainWindow 里存进去)
    int unreadCount = index.data(RoleUnread).toInt();

    // 2. 绘制背景 (模拟 CSS 的 hover 和 selected 效果)
    QRect rect = option.rect;
    rect.adjust(5, 2, -5, -2); // 稍微留点边距

    if (option.state & QStyle::State_Selected) {
        painter->setBrush(QColor(0x39, 0x3c, 0x43)); // 选中色
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setBrush(QColor(0x35, 0x37, 0x3c)); // 悬停色
    } else {
        painter->setBrush(Qt::transparent);
    }
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(rect, 5, 5);

    // 3. 绘制文字 (左侧)
    QString fullText = index.data(Qt::DisplayRole).toString();

    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::white);                // 选中变亮 (白)
    } else {
        painter->setPen(QColor(0x8e, 0x92, 0x97)); // 平常变暗 (灰)
    }

    painter->setFont(QFont("Microsoft YaHei", 12));

    // 文字垂直居中，左对齐，留出左边距
    QRect textRect = rect.adjusted(10, 0, -30, 0); // 右边减30留给红点
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fullText);

    // 4. 【核心】绘制红点 (右侧)
    if (unreadCount > 0) {
        int dotSize = 18;
        // 计算红点位置：在 Item 最右侧偏左一点，垂直居中
        int x = rect.right() - dotSize - 10;
        int y = rect.center().y() - dotSize / 2;
        QRect dotRect(x, y, dotSize, dotSize);

        // 画红圆
        painter->setBrush(QColor(0xF0, 0x47, 0x47)); // Discord 红色
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(dotRect);

        // 画数字
        painter->setPen(Qt::white);
        QFont countFont("Arial", 8, QFont::Bold);
        painter->setFont(countFont);

        QString countText = unreadCount > 99 ? "99+" : QString::number(unreadCount);
        painter->drawText(dotRect, Qt::AlignCenter, countText);
    }

    bool isFriend = index.data(RoleIsFriend).toBool();


    // 绘制添加好友按钮(不是好友时才显示添加好友按钮)
    if (!isFriend) {
        QRect rect = option.rect.adjusted(5, 5, -5, -5);

        // 按钮区域：右下角
        int btnWidth = 80;
        int btnHeight = 28;
        int margin = 10;
        QRect btnRect(
            rect.right() - btnWidth - margin,
            rect.bottom() - btnHeight - margin,
            btnWidth,
            btnHeight
            );

        // 1. 正常颜色：使用品牌蓝 #5865F2 (与发送按钮一致)
        QColor normalColor(0x58, 0x65, 0xF2);

        // 2. 悬停颜色：稍微深一点的蓝 #4752c4 (增加交互感)
        QColor hoverColor(0x47, 0x52, 0xc4);

        if (option.state & QStyle::State_MouseOver) {
            painter->setBrush(hoverColor);
            painter->setPen(Qt::NoPen);
        } else {
            painter->setBrush(normalColor);
            painter->setPen(Qt::NoPen);
        }

        // 绘制扁平化圆角矩形
        painter->drawRoundedRect(btnRect, 4, 4);

        // 文字
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
        painter->drawText(btnRect, Qt::AlignCenter, "添加好友");
    }

    painter->restore();
}

QSize ContactDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    // 固定每一行的高度，与之前 setSizeHint(200,60) 对应
    return QSize(200, 60);
}
