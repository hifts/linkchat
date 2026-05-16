#ifndef TIMEFORMATTER_H
#define TIMEFORMATTER_H

#include <QDateTime>
#include <QString>

QString formatConversationTime(const QDateTime &time);
QString formatChatTimestamp(const QDateTime &time);

#endif // TIMEFORMATTER_H
