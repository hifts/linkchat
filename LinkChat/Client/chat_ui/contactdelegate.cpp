#include "contactdelegate.h"

#include "timeformatter.h"

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
    int onlineStatus = index.data(RoleOnlineStatus).toInt();
    QString preview = index.data(RoleLastMessagePreview).toString();

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

    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::white);
    } else {
        painter->setPen(QColor(0x8e, 0x92, 0x97));
    }

    painter->setFont(QFont("Microsoft YaHei", 12));

    bool isFriend = index.data(RoleIsFriend).toBool();
    int rightMargin = 25;
    
    if (isFriend && lastMsgTime.isValid() && showTime) {
        QString timeText = formatConversationTime(lastMsgTime);
        
        QFont timeFont("Microsoft YaHei", 7);
        QFontMetrics timeFm(timeFont);
        int timeWidth = timeFm.horizontalAdvance(timeText);
        
        rightMargin = timeWidth + 25 + 8;
    }
    
    const bool isGroup = status < 0;
    const int indicatorSize = 7;
    const int leftPadding = 12;
    const int textGap = 10;
    int textLeft = rect.left() + leftPadding;

    if (isFriend && !isGroup) {
        QRect statusRect(textLeft, rect.center().y() - indicatorSize / 2, indicatorSize, indicatorSize);
        QColor dotColor = onlineStatus == 1 ? QColor(0x27, 0xae, 0x60) : QColor(0xff, 0x4d, 0x4f);

        painter->setBrush(dotColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(statusRect);
        textLeft = statusRect.right() + textGap;

        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::white);
        } else {
            painter->setPen(QColor(0x8e, 0x92, 0x97));
        }
    }

    QRect nameRect(textLeft, preview.isEmpty() ? rect.top() : rect.top() + 8, rect.right() - rightMargin - textLeft, preview.isEmpty() ? rect.height() : 22);
    
    QFontMetrics fm(painter->font());
    QString elidedText = fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width());
    painter->drawText(nameRect, preview.isEmpty() ? (Qt::AlignVCenter | Qt::AlignLeft) : (Qt::AlignLeft | Qt::AlignVCenter), elidedText);

    if (!preview.isEmpty()) {
        QFont previewFont("Microsoft YaHei", 8);
        painter->setFont(previewFont);
        painter->setPen(QColor(0x72, 0x76, 0x7d));
        QFontMetrics previewFm(previewFont);
        QRect previewRect(textLeft, rect.top() + 31, rect.right() - rightMargin - textLeft, 18);
        painter->drawText(previewRect, Qt::AlignLeft | Qt::AlignVCenter,
                          previewFm.elidedText(preview, Qt::ElideRight, previewRect.width()));
    }

    if (isFriend && lastMsgTime.isValid() && showTime) {
        QString timeText = formatConversationTime(lastMsgTime);
        
        painter->setFont(QFont("Microsoft YaHei", 7));
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

    painter->restore();
}

QSize ContactDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(300, 60);
}

