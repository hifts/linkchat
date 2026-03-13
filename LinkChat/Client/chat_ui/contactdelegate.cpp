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

    QString name = index.data(RoleName).toString();
    int status = index.data(RoleStatus).toInt();

    int unreadCount = index.data(RoleUnread).toInt();
    
    QDateTime lastMsgTime = index.data(RoleLastMsgTime).toDateTime();
    
    bool showTime = index.data(RoleShowTime).toBool();

    QRect rect = option.rect;
    rect.adjust(5, 2, -5, -2);

    if (option.state & QStyle::State_Selected) {
        painter->setBrush(QColor(0x39, 0x3c, 0x43));
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setBrush(QColor(0x35, 0x37, 0x3c));
    } else {
        painter->setBrush(Qt::transparent);
    }
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(rect, 5, 5);

    QString fullText = index.data(Qt::DisplayRole).toString();

    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::white);
    } else {
        painter->setPen(QColor(0x8e, 0x92, 0x97));
    }

    painter->setFont(QFont("Microsoft YaHei", 12));

    bool isFriend = index.data(RoleIsFriend).toBool();
    int rightMargin = 25;
    
    if (isFriend && lastMsgTime.isValid() && showTime) {
        QString timeText = formatMessageTime(lastMsgTime);
        
        QFont timeFont("Microsoft YaHei", 9);
        QFontMetrics timeFm(timeFont);
        int timeWidth = timeFm.horizontalAdvance(timeText);
        
        rightMargin = timeWidth + 25 + 8;
    }
    
    if (!isFriend) {
        int btnWidth = 65;
        int margin = 8;
        rightMargin = btnWidth + margin + 8;
    }

    QRect textRect = rect.adjusted(12, 0, -rightMargin, 0);
    
    QFontMetrics fm(painter->font());
    QString elidedText = fm.elidedText(fullText, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedText);

    if (isFriend && lastMsgTime.isValid() && showTime) {
        QString timeText = formatMessageTime(lastMsgTime);
        
        painter->setFont(QFont("Microsoft YaHei", 9));
        painter->setPen(QColor(0x72, 0x76, 0x7d));
        
        QFontMetrics timeFm(painter->font());
        int timeWidth = timeFm.horizontalAdvance(timeText);
        
        int timeX = rect.right() - 25 - timeWidth - 5;
        QRect timeRect(timeX, rect.top(), timeWidth, rect.height());
        painter->drawText(timeRect, Qt::AlignVCenter | Qt::AlignRight, timeText);
    }

    if (isFriend && unreadCount > 0) {
        int dotSize = 12;
        int x = rect.right() - dotSize - 8;
        int y = rect.center().y() - dotSize / 2;
        QRect dotRect(x, y, dotSize, dotSize);

        painter->setBrush(QColor(0xFF, 0x4D, 0x4F));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(dotRect);
        
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 7, QFont::Bold));
        QString countText = unreadCount > 99 ? "99+" : QString::number(unreadCount);
        painter->drawText(dotRect, Qt::AlignCenter, countText);
    }

    if (!isFriend) {
        int btnWidth = 65;
        int btnHeight = 24;
        int margin = 8;
        QRect btnRect(
            rect.right() - btnWidth - margin,
            rect.center().y() - btnHeight / 2,
            btnWidth,
            btnHeight
            );

        QColor normalColor(0x58, 0x65, 0xF2);

        QColor hoverColor(0x47, 0x52, 0xc4);

        if (option.state & QStyle::State_MouseOver) {
            painter->setBrush(hoverColor);
            painter->setPen(Qt::NoPen);
        } else {
            painter->setBrush(normalColor);
            painter->setPen(Qt::NoPen);
        }

        painter->drawRoundedRect(btnRect, 4, 4);

        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 8, QFont::Normal));
        painter->drawText(btnRect, Qt::AlignCenter, "添加好友");
    }

    painter->restore();
}

QSize ContactDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(300, 60);
}

QString ContactDelegate::formatMessageTime(const QDateTime &msgTime) const
{
    if (!msgTime.isValid()) {
        return "";
    }
    
    QDateTime now = QDateTime::currentDateTime();
    QDate msgDate = msgTime.date();
    QDate today = now.date();
    
    int daysDiff = msgDate.daysTo(today);
    
    if (daysDiff == 0) {
        return msgTime.toString("HH:mm");
    } else if (daysDiff == 1) {
        return "昨天";
    } else if (daysDiff == 2) {
        return "前天";
    } else if (daysDiff < 7) {
        QStringList weekDays = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        return weekDays[msgDate.dayOfWeek() % 7];
    } else {
        return msgTime.toString("M月d日");
    }
}

