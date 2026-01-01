#include "contactdelegate.h"

#include <QPainter>
#include <QDateTime>

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
    
    // 获取最后消息时间
    QDateTime lastMsgTime = index.data(RoleLastMsgTime).toDateTime();
    
    // 是否显示时间（会话模式）
    bool showTime = index.data(RoleShowTime).toBool();

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

    // 判断是否是好友，决定文字区域的右边界
    bool isFriend = index.data(RoleIsFriend).toBool();
    int rightMargin = 25; // 默认右边距（用于未读消息红点）
    
    // 如果是好友且有时间信息且在会话模式下，需要为时间预留空间
    if (isFriend && lastMsgTime.isValid() && showTime) {
        // 格式化时间显示
        QString timeText = formatMessageTime(lastMsgTime);
        
        // 计算时间文本的宽度
        QFont timeFont("Microsoft YaHei", 9);
        QFontMetrics timeFm(timeFont);
        int timeWidth = timeFm.horizontalAdvance(timeText);
        
        // 为时间预留空间：时间宽度 + 红点空间 + 间距
        rightMargin = timeWidth + 25 + 8; // 时间宽度 + 红点空间 + 额外间距
    }
    
    // 如果不是好友，需要为"添加好友"按钮预留空间
    if (!isFriend) {
        int btnWidth = 65;  // 按钮宽度
        int margin = 8;
        rightMargin = btnWidth + margin + 8; // 按钮宽度 + 边距 + 额外间隙
    }

    // 文字垂直居中，左对齐，根据情况调整右边距
    QRect textRect = rect.adjusted(12, 0, -rightMargin, 0); // 增加左边距，给文字更多空间
    
    // 使用省略号处理过长的文本，但现在有更多空间
    QFontMetrics fm(painter->font());
    QString elidedText = fm.elidedText(fullText, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedText);

    // 3.5 绘制时间 (在会话模式下，显示在用户名的右侧)
    if (isFriend && lastMsgTime.isValid() && showTime) {
        // 格式化时间显示
        QString timeText = formatMessageTime(lastMsgTime);
        
        // 设置较小的字体和灰色
        painter->setFont(QFont("Microsoft YaHei", 9));
        painter->setPen(QColor(0x72, 0x76, 0x7d)); // 灰色
        
        // 计算时间文本的宽度
        QFontMetrics timeFm(painter->font());
        int timeWidth = timeFm.horizontalAdvance(timeText);
        
        // 在右侧显示时间（留出未读红点的空间）
        int timeX = rect.right() - 25 - timeWidth - 5; // 25是红点空间，5px额外间距
        QRect timeRect(timeX, rect.top(), timeWidth, rect.height());
        painter->drawText(timeRect, Qt::AlignVCenter | Qt::AlignRight, timeText);
    }

    // 4. 【核心】绘制红点 (右侧) - 只在是好友时显示
    if (isFriend && unreadCount > 0) {
        int dotSize = 12; // 调小红点尺寸
        // 计算红点位置：在 Item 最右侧偏左一点，垂直居中
        int x = rect.right() - dotSize - 8;
        int y = rect.center().y() - dotSize / 2;
        QRect dotRect(x, y, dotSize, dotSize);

        // 画红色圆点 - 使用醒目的红色
        painter->setBrush(QColor(0xFF, 0x4D, 0x4F)); // 鲜红色
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(dotRect);
        
        // 显示未读数字（包括只有1条消息的情况）
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 7, QFont::Bold)); // 调小字体
        QString countText = unreadCount > 99 ? "99+" : QString::number(unreadCount);
        painter->drawText(dotRect, Qt::AlignCenter, countText);
    }

    // 绘制添加好友按钮(不是好友时才显示添加好友按钮)
    if (!isFriend) {
        // 按钮区域：右侧垂直居中，尺寸更精致
        int btnWidth = 65;   // 减小宽度
        int btnHeight = 24;  // 减小高度
        int margin = 8;
        QRect btnRect(
            rect.right() - btnWidth - margin,
            rect.center().y() - btnHeight / 2, // 垂直居中
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

        // 文字 - 使用更小的字体
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 8, QFont::Normal)); // 减小字体大小，去掉加粗
        painter->drawText(btnRect, Qt::AlignCenter, "添加好友");
    }

    painter->restore();
}

QSize ContactDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    // 固定每一行的高度，增加宽度以适应更长的用户名
    return QSize(300, 60); // 从200增加到300
}

QString ContactDelegate::formatMessageTime(const QDateTime &msgTime) const
{
    if (!msgTime.isValid()) {
        return "";
    }
    
    QDateTime now = QDateTime::currentDateTime();
    QDate msgDate = msgTime.date();
    QDate today = now.date();
    
    // 计算日期差
    int daysDiff = msgDate.daysTo(today);
    
    if (daysDiff == 0) {
        // 今天 - 显示时间
        return msgTime.toString("HH:mm");
    } else if (daysDiff == 1) {
        // 昨天
        return "昨天";
    } else if (daysDiff == 2) {
        // 前天
        return "前天";
    } else if (daysDiff < 7) {
        // 一周内 - 显示星期几
        QStringList weekDays = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        return weekDays[msgDate.dayOfWeek() % 7];
    } else {
        // 更早 - 显示月日
        return msgTime.toString("M月d日");
    }
}
