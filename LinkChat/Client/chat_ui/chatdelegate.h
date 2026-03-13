#ifndef CHATDELEGATE_H
#define CHATDELEGATE_H

#include <QStyledItemDelegate>

class ChatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChatDelegate(QObject *parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    const int m_avatarSize = 40;
    const int m_margin = 10;
    const int m_bubblePadding = 10;
    const int m_maxBubbleWidthRatio = 70;

    const int m_maxImageWidth = 100;
    const int m_maxImageHeight = 100;
    const int m_timeGap = 300;
    const int m_timeHeight = 20;
    
    int calculateMaxBubbleWidth(int viewWidth) const;
    
    bool shouldShowTimestamp(const QModelIndex &index) const;
signals:


};

#endif // CHATDELEGATE_H
