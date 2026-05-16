#include "chatdelegate.h"
#include "chatmodel.h"
#include "timeformatter.h"

#include <QPainter>
#include <QPainterPath>
#include <QDateTime>

ChatDelegate::ChatDelegate(QObject *parent)
    : QStyledItemDelegate{parent}
{}

int ChatDelegate::calculateMaxBubbleWidth(int viewWidth) const
{
    // 气泡最大宽度 = 视图宽度的70% - 头像 - 边距
    int maxWidth = (viewWidth * m_maxBubbleWidthRatio / 100) - m_avatarSize - m_margin * 3;
    // 设置最小宽度为200，最大宽度为600
    return qBound(200, maxWidth, 600);
}

bool ChatDelegate::shouldShowTimestamp(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    
    // 第一条消息总是显示时间
    if (index.row() == 0) {
        return true;
    }
    
    // 获取当前消息和上一条消息的时间戳
    quint64 currentTimestamp = index.data(ChatModel::RoleTimestamp).toULongLong();
    QModelIndex prevIndex = index.sibling(index.row() - 1, 0);
    quint64 prevTimestamp = prevIndex.data(ChatModel::RoleTimestamp).toULongLong();
    
    // 如果时间戳为0，不显示
    if (currentTimestamp == 0) {
        return false;
    }
    
    // 如果时间差超过5分钟（300秒），显示时间
    return (currentTimestamp - prevTimestamp) > m_timeGap;
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    MsgType type = (MsgType)index.data(ChatModel::RoleType).toInt();
    bool isGroupChat = index.data(ChatModel::RoleIsGroupChat).toBool();
    bool isMine = index.data(ChatModel::RoleIsMine).toBool();
    int senderNameHeight = (isGroupChat && !isMine) ? 18 : 0; // 群聊非自己的消息显示发送者用户名
    
    // 检查是否需要显示时间戳
    int timestampHeight = shouldShowTimestamp(index) ? m_timeHeight : 0;

    if (type == TypeFile) {
        int cardHeight = 64;
        int h = cardHeight + m_margin * 2 + senderNameHeight + timestampHeight;
        return QSize(0, qMax(h, m_avatarSize + m_margin * 2 + senderNameHeight + timestampHeight));
    } else if (type == TypeText) {
        QString text = index.data(ChatModel::RoleContent).toString();
        QFontMetrics fm(option.font);
        
        // 根据视图宽度动态计算最大文本宽度
        int maxBubbleWidth = calculateMaxBubbleWidth(option.rect.width());
        int maxTextWidth = maxBubbleWidth - m_bubblePadding * 2;
        
        QRect r = fm.boundingRect(0, 0, maxTextWidth, 10000, Qt::TextWordWrap, text);
        int h = r.height() + m_bubblePadding * 2 + m_margin * 2 + senderNameHeight + timestampHeight;
        return QSize(0, qMax(h, m_avatarSize + m_margin * 2 + senderNameHeight + timestampHeight));
    } else {
        return QSize(0, m_maxImageHeight + m_margin * 2 + 20 + senderNameHeight + timestampHeight);
    }
}

void ChatDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    bool isMine = index.data(ChatModel::RoleIsMine).toBool();
    MsgType msgType = (MsgType)index.data(ChatModel::RoleType).toInt();
    QString avatarPath = index.data(ChatModel::RoleAvatar).toString();
    bool isGroupChat = index.data(ChatModel::RoleIsGroupChat).toBool();
    QString senderName = index.data(ChatModel::RoleSenderName).toString();
    int encryptionStatus = index.data(ChatModel::RoleEncryptionStatus).toInt();
    quint64 timestamp = index.data(ChatModel::RoleTimestamp).toULongLong();

    QRect rect = option.rect;
    int senderNameHeight = (isGroupChat && !isMine) ? 18 : 0; // 群聊非自己的消息显示发送者用户名
    
    // 检查是否需要显示时间戳
    int timestampHeight = 0;
    if (shouldShowTimestamp(index)) {
        timestampHeight = m_timeHeight;
        
        // 绘制时间戳（居中显示）- 智能显示日期
        QDateTime msgDateTime = QDateTime::fromSecsSinceEpoch(timestamp);
        QDateTime now = QDateTime::currentDateTime();
        QDate msgDate = msgDateTime.date();
        QDate today = now.date();
        
        QString timeStr;
        int daysDiff = msgDate.daysTo(today);
        
        if (daysDiff == 0) {
            // 今天：只显示时间
            timeStr = msgDateTime.toString("hh:mm");
        } else if (daysDiff == 1) {
            // 昨天
            timeStr = QString("昨天 %1").arg(msgDateTime.toString("hh:mm"));
        } else if (daysDiff == 2) {
            // 前天
            timeStr = QString("前天 %1").arg(msgDateTime.toString("hh:mm"));
        } else if (msgDate.year() == today.year()) {
            // 今年：显示月-日 时:分
            timeStr = msgDateTime.toString("MM-dd hh:mm");
        } else {
            // 往年：显示完整日期
            timeStr = msgDateTime.toString("yyyy-MM-dd hh:mm");
        }
        timeStr = formatChatTimestamp(msgDateTime);

        QFont timeFont = option.font;
        timeFont.setPointSize(7);
        painter->setFont(timeFont);
        painter->setPen(QColor(0x999999)); // 灰色
        
        QRect timeRect(rect.left(), rect.top() + 2, rect.width(), m_timeHeight);
        painter->drawText(timeRect, Qt::AlignCenter, timeStr);
        
        painter->setFont(option.font);
    }

    // 1. 绘制头像（不变）
    QRect avatarRect = isMine
                           ? QRect(rect.right() - m_margin - m_avatarSize, rect.top() + m_margin + timestampHeight, m_avatarSize, m_avatarSize)
                           : QRect(rect.left() + m_margin, rect.top() + m_margin + timestampHeight, m_avatarSize, m_avatarSize);

    QPainterPath path;
    path.addEllipse(avatarRect);
    painter->setClipPath(path);
    painter->drawPixmap(avatarRect, QPixmap(avatarPath).scaled(avatarRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    painter->setClipping(false);

    // 2. 计算内容区域
    QRect contentRect;
    int contentWidth = 0;
    int contentHeight = 0;

    if (msgType == TypeFile) {
        QString text = index.data(ChatModel::RoleContent).toString();
        const QStringList parts = text.split('\n');
        const QString fileName = parts.value(0);
        const QString detail = parts.value(1);

        const int cardWidth = qMin(calculateMaxBubbleWidth(rect.width()), 280);
        const int cardHeight = 64;
        contentRect = isMine
                          ? QRect(avatarRect.left() - m_margin - cardWidth, rect.top() + m_margin + senderNameHeight + timestampHeight, cardWidth, cardHeight)
                          : QRect(avatarRect.right() + m_margin, rect.top() + m_margin + senderNameHeight + timestampHeight, cardWidth, cardHeight);

        if (isGroupChat && !isMine && !senderName.isEmpty()) {
            QFont nameFont = option.font;
            nameFont.setPointSize(10);
            painter->setFont(nameFont);
            painter->setPen(QColor(0x8e9297));
            QRect nameRect(contentRect.left() + 4, contentRect.top() - senderNameHeight - 2, contentRect.width(), senderNameHeight);
            painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignBottom, senderName);
            painter->setFont(option.font);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(isMine ? QColor(0x5865f2) : QColor(0x40444b));
        painter->drawRoundedRect(contentRect, 10, 10);

        QRect iconRect(contentRect.left() + 12, contentRect.top() + 14, 36, 36);
        painter->setBrush(isMine ? QColor(0x7289ff) : QColor(0x5865f2));
        painter->drawRoundedRect(iconRect, 8, 8);

        painter->setPen(QColor(0xffffff));
        QFont iconFont = option.font;
        iconFont.setPointSize(18);
        iconFont.setBold(true);
        painter->setFont(iconFont);
        painter->drawText(iconRect, Qt::AlignCenter, "F");

        QFont titleFont = option.font;
        titleFont.setPointSize(10);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(QColor(0xf4f7fb));
        QRect titleRect(iconRect.right() + 10, contentRect.top() + 12, contentRect.width() - 68, 22);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, fileName);

        QFont detailFont = option.font;
        detailFont.setPointSize(8);
        detailFont.setBold(false);
        painter->setFont(detailFont);
        painter->setPen(isMine ? QColor(0xdde3ff) : QColor(0xb9bbbe));
        QRect detailRect(iconRect.right() + 10, contentRect.top() + 34, contentRect.width() - 68, 18);
        painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter, detail.isEmpty() ? "文件传输" : detail);

        painter->setFont(option.font);

    } else if (msgType == TypeText) {
        QString text = index.data(ChatModel::RoleContent).toString();
        QFontMetrics fm(option.font);
        
        // 根据视图宽度动态计算最大文本宽度
        int maxBubbleWidth = calculateMaxBubbleWidth(rect.width());
        int maxTextWidth = maxBubbleWidth - m_bubblePadding * 2;
        
        QRect textRect = fm.boundingRect(0, 0, maxTextWidth, 10000, Qt::TextWordWrap, text);

        contentWidth = textRect.width() + m_bubblePadding * 2;
        contentHeight = textRect.height() + m_bubblePadding * 2;

        contentRect = isMine
                          ? QRect(avatarRect.left() - m_margin - contentWidth, rect.top() + m_margin + senderNameHeight + timestampHeight, contentWidth, contentHeight)
                          : QRect(avatarRect.right() + m_margin, rect.top() + m_margin + senderNameHeight + timestampHeight, contentWidth, contentHeight);

        // 群聊非自己的消息：在气泡上方显示发送者用户名
        if (isGroupChat && !isMine && !senderName.isEmpty()) {
            QFont nameFont = option.font;
            nameFont.setPointSize(10);  // 字体稍大一点
            nameFont.setBold(false);
            painter->setFont(nameFont);
            painter->setPen(QColor(0x8e9297)); // 浅灰色
            // 与气泡保持 4px 的间距
            QRect nameRect(contentRect.left() + 4, contentRect.top() - senderNameHeight - 2, contentRect.width(), senderNameHeight);
            painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignBottom, senderName);
            painter->setFont(option.font);
        }

        // 画气泡
        painter->setBrush(isMine ? QColor(0x95ec69) : QColor(0xffffff));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(contentRect, 12, 12);

        // 画文字 - 使用 Qt::TextWordWrap 确保自动换行
        painter->setPen(Qt::black);
        QRect textDrawRect = contentRect.adjusted(m_bubblePadding, m_bubblePadding, -m_bubblePadding, -m_bubblePadding);
        painter->drawText(textDrawRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
        
        // 绘制加密状态指示器（在气泡右下角）
        if (encryptionStatus == 1) { // Encrypted
            // 绘制小锁图标（使用Unicode字符）
            QFont iconFont = option.font;
            iconFont.setPointSize(8);
            painter->setFont(iconFont);
            painter->setPen(QColor(0x5865F2)); // 蓝色表示加密
            QRect lockRect(contentRect.right() - 16, contentRect.bottom() - 14, 14, 12);
            painter->drawText(lockRect, Qt::AlignCenter, "🔒");
            painter->setFont(option.font);
        } else if (encryptionStatus == 2) { // Unencrypted
            // 绘制警告图标
            QFont iconFont = option.font;
            iconFont.setPointSize(8);
            painter->setFont(iconFont);
            painter->setPen(QColor(0xFAA61A)); // 橙色表示未加密
            QRect warnRect(contentRect.right() - 16, contentRect.bottom() - 14, 14, 12);
            painter->drawText(warnRect, Qt::AlignCenter, "⚠");
            painter->setFont(option.font);
        }

    } else if (msgType == TypeImage) {
        QByteArray raw = index.data(ChatModel::RoleImage).toByteArray();

        QImage image;
        if (raw.isEmpty() || !image.loadFromData(raw)) {
            // 加载失败显示占位图
            painter->drawPixmap(rect, QPixmap(":/res/image_broken.jpg"));
        } else {
            int imgW = qMin(image.width(), m_maxImageWidth);
            int imgH = qMin(image.height(), m_maxImageHeight);
            if (image.width() > image.height())
                imgH = image.height() * m_maxImageWidth / image.width();
            else
                imgW = image.width() * m_maxImageHeight / image.height();

            QRect bubbleRect = isMine
                                   ? QRect(avatarRect.left() - m_margin - imgW - 10, rect.top() + m_margin + timestampHeight, imgW + 10, imgH + 10)
                                   : QRect(avatarRect.right() + m_margin, rect.top() + m_margin + timestampHeight, imgW + 10, imgH + 10);

            // 轻微圆角 + 浅边框
            // painter->setBrush(Qt::white);
            // painter->setPen(QPen(QColor(220,220,220), 1));
            painter->drawRoundedRect(bubbleRect.adjusted(5,5,-5,-5), 8, 8);

            QRect imgShowRect = bubbleRect.adjusted(5,5,-5,-5);
            painter->drawImage(imgShowRect, image,image.rect()); // 自动缩放
            
            // 绘制加密状态指示器（在图片右下角）
            if (encryptionStatus == 1) { // Encrypted
                QFont iconFont = option.font;
                iconFont.setPointSize(8);
                painter->setFont(iconFont);
                painter->setPen(QColor(0x5865F2));
                QRect lockRect(bubbleRect.right() - 20, bubbleRect.bottom() - 18, 14, 12);
                painter->drawText(lockRect, Qt::AlignCenter, "🔒");
                painter->setFont(option.font);
            } else if (encryptionStatus == 2) { // Unencrypted
                QFont iconFont = option.font;
                iconFont.setPointSize(8);
                painter->setFont(iconFont);
                painter->setPen(QColor(0xFAA61A));
                QRect warnRect(bubbleRect.right() - 20, bubbleRect.bottom() - 18, 14, 12);
                painter->drawText(warnRect, Qt::AlignCenter, "⚠");
                painter->setFont(option.font);
            }
        }
    }

    painter->restore();
}
