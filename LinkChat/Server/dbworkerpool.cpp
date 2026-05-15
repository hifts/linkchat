#include "dbworkerpool.h"

#include "configkeys.h"
#include "logger.h"
#include "serverstats.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

class DbRunnable : public QRunnable
{
public:
    DbRunnable(DbWorkerPool::Task task, QObject* receiver, DbWorkerPool::Callback callback)
        : m_task(std::move(task))
        , m_receiver(receiver)
        , m_callback(std::move(callback))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        ServerStats::instance().dbTaskStarted();
        QElapsedTimer timer;
        timer.start();

        QVariant result;
        DBManager manager;
        const QString connectionName = QString("linkchat_worker_%1")
                                           .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        bool connected = manager.connectToDb(connectionName);
        if (!connected) {
            connected = manager.connectToDb(connectionName);
        }
        if (connected && m_task) {
            result = m_task(manager);
        } else if (!connected) {
            LOG_ERROR("DB worker failed to connect database");
        }

        ServerStats::instance().dbTaskFinished(timer.elapsed());

        if (m_receiver && m_callback) {
            QPointer<QObject> safeReceiver = m_receiver;
            auto callback = std::move(m_callback);
            QMetaObject::invokeMethod(safeReceiver, [safeReceiver, callback, result]() {
                if (!safeReceiver) {
                    return;
                }
                callback(result);
            }, Qt::QueuedConnection);
        }
    }

private:
    DbWorkerPool::Task m_task;
    QPointer<QObject> m_receiver;
    DbWorkerPool::Callback m_callback;
};

DbWorkerPool& DbWorkerPool::instance()
{
    static DbWorkerPool pool;
    return pool;
}

DbWorkerPool::DbWorkerPool(QObject *parent)
    : QObject(parent)
{
}

void DbWorkerPool::start(int workerCount)
{
    m_pool.setMaxThreadCount(qMax(1, workerCount));
    m_running = true;
    LOG_WARN(QString("DB worker pool started with %1 thread(s)").arg(m_pool.maxThreadCount()));
}

bool DbWorkerPool::healthCheck()
{
    DBManager manager;
    const QString connectionName = QString("linkchat_health_%1")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    bool ok = manager.connectToDb(connectionName);
    if (!ok) {
        ok = manager.connectToDb(connectionName);
    }
    if (ok) {
        LOG_WARN("DB worker health check passed");
    } else {
        LOG_ERROR("DB worker health check failed");
    }
    return ok;
}

void DbWorkerPool::stop()
{
    m_running = false;
    m_pool.waitForDone();
}

bool DbWorkerPool::isRunning() const
{
    return m_running;
}

void DbWorkerPool::enqueue(Task task, QObject* receiver, Callback callback)
{
    if (!m_running) {
        LOG_ERROR("DB worker pool is not running");
        return;
    }
    ServerStats::instance().dbTaskQueued();
    m_pool.start(new DbRunnable(std::move(task), receiver, std::move(callback)));
}
