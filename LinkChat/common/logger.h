#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QDebug>
#include <QDir>

/**
 * @brief 日志系统 - 支持多级别日志输出到文件和控制台
 * 
 * 使用示例:
 *   Logger::init("client.log");
 *   LOG_INFO("用户登录成功");
 *   LOG_ERROR("网络连接失败");
 */
class Logger
{
public:
    // 日志级别
    enum Level {
        DEBUG = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3
    };

    /**
     * @brief 初始化日志系统
     * @param logFileName 日志文件名（默认 app.log）
     * @param minLevel 最小日志级别（低于此级别的不输出）
     */
    static void init(const QString &logFileName = "app.log", Level minLevel = DEBUG) {
        instance().m_logFileName = logFileName;
        instance().m_minLevel = minLevel;
        instance().m_initialized = true;
        
        // 确保日志目录存在
        QFileInfo fileInfo(logFileName);
        if (!fileInfo.absoluteDir().exists()) {
            QDir().mkpath(fileInfo.absolutePath());
        }
        
        // 写入启动日志
        log(INFO, "========== 日志系统启动 ==========");
    }

    /**
     * @brief 写入日志
     * @param level 日志级别
     * @param message 日志内容
     * @param file 源文件名（由宏自动填充）
     * @param line 行号（由宏自动填充）
     */
    static void log(Level level, const QString &message, 
                    const char *file = nullptr, int line = 0) {
        if (!instance().m_initialized || level < instance().m_minLevel) {
            return;
        }

        // 格式化时间戳
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        
        // 格式化级别
        QString levelStr = levelToString(level);
        
        // 格式化日志行
        QString logLine;
        if (file && line > 0) {
            // 只保留文件名，不要完整路径
            QString fileName = QFileInfo(file).fileName();
            logLine = QString("[%1][%2][%3:%4] %5")
                .arg(timestamp, levelStr, fileName)
                .arg(line)
                .arg(message);
        } else {
            logLine = QString("[%1][%2] %3")
                .arg(timestamp, levelStr, message);
        }

        // 线程安全：加锁
        QMutexLocker locker(&instance().m_mutex);

        // 写入文件
        QFile file_out(instance().m_logFileName);
        if (file_out.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file_out);
            stream.setCodec("UTF-8");
            stream << logLine << "\n";
            file_out.close();
        }

        // 输出到控制台（带颜色）
        switch (level) {
        case DEBUG:
            qDebug().noquote() << logLine;
            break;
        case INFO:
            qInfo().noquote() << logLine;
            break;
        case WARN:
            qWarning().noquote() << logLine;
            break;
        case ERROR:
            qCritical().noquote() << logLine;
            break;
        }
    }

    /**
     * @brief 设置最小日志级别
     */
    static void setMinLevel(Level level) {
        instance().m_minLevel = level;
    }

    /**
     * @brief 将字符串转换为日志级别
     * @param levelStr 日志级别字符串（DEBUG/INFO/WARNING/ERROR，不区分大小写）
     * @return Level 对应的日志级别，无效字符串返回 INFO
     */
    static Level stringToLevel(const QString& levelStr) {
        QString upper = levelStr.toUpper();
        if (upper == "DEBUG") return DEBUG;
        if (upper == "INFO") return INFO;
        if (upper == "WARN" || upper == "WARNING") return WARN;
        if (upper == "ERROR") return ERROR;
        return INFO; // 默认返回 INFO
    }

private:
    Logger() : m_initialized(false), m_minLevel(DEBUG) {}
    
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    static QString levelToString(Level level) {
        switch (level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default:    return "UNKNW";
        }
    }

    QString m_logFileName;
    Level m_minLevel;
    bool m_initialized;
    QMutex m_mutex;
};

// 使用这些宏可以自动记录文件名和行号

#define LOG_DEBUG(msg) Logger::log(Logger::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::log(Logger::INFO, msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::log(Logger::WARN, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::log(Logger::ERROR, msg, __FILE__, __LINE__)

// 格式化版本（支持 QString::arg）
#define LOG_DEBUG_FMT(fmt, ...) Logger::log(Logger::DEBUG, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_INFO_FMT(fmt, ...)  Logger::log(Logger::INFO, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_WARN_FMT(fmt, ...)  Logger::log(Logger::WARN, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_ERROR_FMT(fmt, ...) Logger::log(Logger::ERROR, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)

#endif // LOGGER_H
