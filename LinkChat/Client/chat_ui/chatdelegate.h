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
    const int m_maxBubbleWidthRatio = 70; // 气泡最大宽度占窗口的百分比

    const int m_maxImageWidth = 100;    // 图片显示的最大宽度
    const int m_maxImageHeight = 100;   // 图片显示的最大高度
    const int m_timeGap = 300;          // 时间间隔阈值（秒），5分钟 = 300秒
    const int m_timeHeight = 20;        // 时间标签高度
    
    // 计算气泡最大宽度（根据视图宽度动态计算）
    int calculateMaxBubbleWidth(int viewWidth) const;
    
    // 判断是否需要显示时间戳
    bool shouldShowTimestamp(const QModelIndex &index) const;
signals:


};

#endif // CHATDELEGATE_H
