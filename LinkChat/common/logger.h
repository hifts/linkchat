#ifndef LOGGER_H
#define LOGGER_H

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QString>
#include <QTextStream>
#include <QWaitCondition>
#include <QDebug>

#include <thread>

class Logger
{
public:
    enum Level {
        DEBUG = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3
    };

    static void init(const QString &logFileName = "app.log", Level minLevel = INFO)
    {
        Logger& inst = instance();
        bool wasInitialized = false;
        {
            QMutexLocker locker(&inst.m_mutex);
            wasInitialized = inst.m_initialized;
            inst.m_logFileName = logFileName;
            inst.m_minLevel = minLevel;
            inst.m_initialized = true;

            QFileInfo fileInfo(logFileName);
            if (!fileInfo.absoluteDir().exists()) {
                QDir().mkpath(fileInfo.absolutePath());
            }

            inst.ensureWorkerStartedLocked();
        }

        if (wasInitialized) {
            log(INFO, QString("========== Logger reconfigured: %1 ==========").arg(logFileName));
        } else {
            log(INFO, "========== Logger started ==========");
        }
    }

    static void log(Level level, const QString &message, const char *file = nullptr, int line = 0)
    {
        Logger& inst = instance();
        if (!inst.m_initialized || level < inst.m_minLevel) {
            return;
        }

        const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        const QString levelStr = levelToString(level);

        QString logLine;
        if (file && line > 0) {
            const QString fileName = QFileInfo(file).fileName();
            logLine = QString("[%1][%2][%3:%4] %5")
                          .arg(timestamp, levelStr, fileName)
                          .arg(line)
                          .arg(message);
        } else {
            logLine = QString("[%1][%2] %3").arg(timestamp, levelStr, message);
        }

        {
            QMutexLocker locker(&inst.m_mutex);
            inst.m_queue.enqueue(logLine);
            if (inst.m_queue.size() > inst.m_maxQueueSize) {
                inst.m_queue.dequeue();
            }
            inst.m_wait.wakeOne();
        }

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

    static void setMinLevel(Level level)
    {
        instance().m_minLevel = level;
    }

    static void shutdown()
    {
        Logger& inst = instance();
        {
            QMutexLocker locker(&inst.m_mutex);
            inst.m_stopping = true;
            inst.m_wait.wakeAll();
        }
        if (inst.m_worker.joinable()) {
            inst.m_worker.join();
        }
    }

    static Level stringToLevel(const QString& levelStr)
    {
        const QString upper = levelStr.toUpper();
        if (upper == "DEBUG") return DEBUG;
        if (upper == "INFO") return INFO;
        if (upper == "WARN" || upper == "WARNING") return WARN;
        if (upper == "ERROR") return ERROR;
        return INFO;
    }

private:
    Logger() = default;
    ~Logger()
    {
        shutdown();
    }

    static Logger& instance()
    {
        static Logger instance;
        return instance;
    }

    static QString levelToString(Level level)
    {
        switch (level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default:    return "UNKNOWN";
        }
    }

    void ensureWorkerStartedLocked()
    {
        if (m_worker.joinable()) {
            return;
        }

        m_stopping = false;
        m_worker = std::thread([this]() {
            QFile out;
            QString openedPath;

            while (true) {
                QQueue<QString> batch;
                {
                    QMutexLocker locker(&m_mutex);
                    if (m_queue.isEmpty() && !m_stopping) {
                        m_wait.wait(&m_mutex, 1000);
                    }
                    while (!m_queue.isEmpty() && batch.size() < 512) {
                        batch.enqueue(m_queue.dequeue());
                    }
                    if (m_stopping && batch.isEmpty()) {
                        break;
                    }
                }

                if (batch.isEmpty()) {
                    continue;
                }

                const QString path = m_logFileName;
                if (!out.isOpen() || openedPath != path) {
                    if (out.isOpen()) {
                        out.close();
                    }
                    openedPath = path;
                    out.setFileName(path);
                    out.open(QIODevice::Append | QIODevice::Text);
                }

                if (!out.isOpen()) {
                    continue;
                }

                QTextStream stream(&out);
                stream.setCodec("UTF-8");
                while (!batch.isEmpty()) {
                    stream << batch.dequeue() << "\n";
                }
                stream.flush();
                out.flush();
            }

            if (out.isOpen()) {
                out.close();
            }
        });
    }

    QString m_logFileName;
    Level m_minLevel = INFO;
    bool m_initialized = false;
    bool m_stopping = false;
    QMutex m_mutex;
    QWaitCondition m_wait;
    QQueue<QString> m_queue;
    std::thread m_worker;
    const int m_maxQueueSize = 20000;
};

#define LOG_DEBUG(msg) Logger::log(Logger::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::log(Logger::INFO, msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::log(Logger::WARN, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::log(Logger::ERROR, msg, __FILE__, __LINE__)

#define LOG_DEBUG_FMT(fmt, ...) Logger::log(Logger::DEBUG, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_INFO_FMT(fmt, ...)  Logger::log(Logger::INFO, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_WARN_FMT(fmt, ...)  Logger::log(Logger::WARN, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)
#define LOG_ERROR_FMT(fmt, ...) Logger::log(Logger::ERROR, QString(fmt).arg(__VA_ARGS__), __FILE__, __LINE__)

#endif // LOGGER_H
