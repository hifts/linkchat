#include "chatdelegate.h"
#include "chatmodel.h"

#include <QPainter>
#include <QPainterPath>

ChatDelegate::ChatDelegate(QObject *parent)
    : QStyledItemDelegate{parent}
{}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    MsgType type = (MsgType)index.data(ChatModel::RoleType).toInt();
    bool isGroupChat = index.data(ChatModel::RoleIsGroupChat).toBool();
    bool isMine = index.data(ChatModel::RoleIsMine).toBool();
    int senderNameHeight = (isGroupChat && !isMine) ? 18 : 0; // 群聊非自己的消息显示发送者用户名

    if (type == TypeText) {
        QString text = index.data(ChatModel::RoleContent).toString();
        QFontMetrics fm(option.font);
        int maxW = m_maxWidth - m_avatarSize - m_margin * 3;
        QRect r = fm.boundingRect(0,0,maxW,10000,Qt::TextWordWrap,text);
        int h = r.height() + m_bubblePadding*2 + m_margin*2 + senderNameHeight;
        return QSize(0, qMax(h, m_avatarSize + m_margin*2 + senderNameHeight));
    } else {
        return QSize(0, m_maxImageHeight + m_margin*2 + 20 + senderNameHeight);
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

    QRect rect = option.rect;
    int senderNameHeight = (isGroupChat && !isMine) ? 18 : 0; // 群聊非自己的消息显示发送者用户名

    // 1. 绘制头像（不变）
    QRect avatarRect = isMine
                           ? QRect(rect.right() - m_margin - m_avatarSize, rect.top() + m_margin, m_avatarSize, m_avatarSize)
                           : QRect(rect.left() + m_margin, rect.top() + m_margin, m_avatarSize, m_avatarSize);

    QPainterPath path;
    path.addEllipse(avatarRect);
    painter->setClipPath(path);
    painter->drawPixmap(avatarRect, QPixmap(avatarPath).scaled(avatarRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    painter->setClipping(false);

    // 2. 计算内容区域
    QRect contentRect;
    int contentWidth = 0;
    int contentHeight = 0;

    if (msgType == TypeText) {
        QString text = index.data(ChatModel::RoleContent).toString();
        QFontMetrics fm(option.font);
        int maxTextWidth = m_maxWidth - m_avatarSize - m_margin * 3;
        QRect textRect = fm.boundingRect(0, 0, maxTextWidth, 10000, Qt::TextWordWrap, text);

        contentWidth = textRect.width() + m_bubblePadding * 2;
        contentHeight = textRect.height() + m_bubblePadding * 2;

        contentRect = isMine
                          ? QRect(avatarRect.left() - m_margin - contentWidth, rect.top() + m_margin + senderNameHeight, contentWidth, contentHeight)
                          : QRect(avatarRect.right() + m_margin, rect.top() + m_margin + senderNameHeight, contentWidth, contentHeight);

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

        // 画文字
        painter->setPen(Qt::black);
        QRect textDrawRect = contentRect.adjusted(m_bubblePadding, m_bubblePadding, -m_bubblePadding, -m_bubblePadding);
        painter->drawText(textDrawRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, text);
        
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
                                   ? QRect(avatarRect.left() - m_margin - imgW - 10, rect.top() + m_margin, imgW + 10, imgH + 10)
                                   : QRect(avatarRect.right() + m_margin, rect.top() + m_margin, imgW + 10, imgH + 10);

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
