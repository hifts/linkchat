#include "timeformatter.h"

#include <QDate>
#include <QStringList>

namespace {
QString weekdayName(const QDate &date)
{
    static const QStringList weekDays = {
        QStringLiteral("星期一"),
        QStringLiteral("星期二"),
        QStringLiteral("星期三"),
        QStringLiteral("星期四"),
        QStringLiteral("星期五"),
        QStringLiteral("星期六"),
        QStringLiteral("星期日")
    };
    return weekDays.value(date.dayOfWeek() - 1);
}

QString formatDatePart(const QDateTime &time)
{
    const QDate msgDate = time.date();
    const QDate today = QDate::currentDate();
    const int daysDiff = msgDate.daysTo(today);

    if (daysDiff == 0) {
        return time.toString("HH:mm");
    }
    if (daysDiff == 1) {
        return QStringLiteral("昨天");
    }
    if (daysDiff > 1 && daysDiff < 7) {
        return weekdayName(msgDate);
    }
    if (msgDate.year() == today.year()) {
        return time.toString("M月d日");
    }
    return time.toString("yyyy年M月d日");
}
}

QString formatConversationTime(const QDateTime &time)
{
    if (!time.isValid()) {
        return QString();
    }
    return formatDatePart(time);
}

QString formatChatTimestamp(const QDateTime &time)
{
    if (!time.isValid()) {
        return QString();
    }

    const QDate today = QDate::currentDate();
    if (time.date() == today) {
        return time.toString("HH:mm");
    }

    return QString("%1 %2").arg(formatDatePart(time), time.toString("HH:mm"));
}
