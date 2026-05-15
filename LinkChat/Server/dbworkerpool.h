#ifndef DBWORKERPOOL_H
#define DBWORKERPOOL_H

#include "dbmanager.h"

#include <QObject>
#include <QThreadPool>
#include <QRunnable>
#include <QVariant>
#include <functional>

class DbWorkerPool : public QObject
{
    Q_OBJECT
public:
    using Task = std::function<QVariant(DBManager&)>;
    using Callback = std::function<void(const QVariant&)>;

    static DbWorkerPool& instance();

    void start(int workerCount);
    bool healthCheck();
    void stop();
    bool isRunning() const;
    void enqueue(Task task, QObject* receiver, Callback callback = Callback());

private:
    explicit DbWorkerPool(QObject *parent = nullptr);

    QThreadPool m_pool;
    bool m_running = false;
};

#endif // DBWORKERPOOL_H
