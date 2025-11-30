#ifndef CHATDELEGATE_H
#define CHATDELEGATE_H

#include <QStyledItemDelegate>

class ChatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChatDelegate(QObject *parent = nullptr);

    // 计算每一行需要多高 (根据文字长度自动计算)
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    // 绘制聊天气泡
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    const int m_avatarSize = 40;        // 头像大小
    const int m_margin = 10;            // 间距
    const int m_bubblePadding = 10;     // 气泡内边距
    const int m_maxWidth = 600;         // 气泡最大宽度

    const int m_maxImageWidth = 100;    // 图片显示的最大宽度
    const int m_maxImageHeight = 100;   // 图片显示的最大高度
signals:


};

#endif // CHATDELEGATE_H
