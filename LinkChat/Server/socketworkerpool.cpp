#include "socketworkerpool.h"

#include "logger.h"
#include "socketworker.h"

#include <QThread>

SocketWorkerPool::SocketWorkerPool(QObject *parent)
    : QObject(parent)
{
}

SocketWorkerPool::~SocketWorkerPool()
{
    stop();
}

void SocketWorkerPool::start(int workerCount)
{
    if (!m_workers.isEmpty()) {
        return;
    }

    const int count = qMax(1, workerCount);
    m_threads.reserve(count);
    m_workers.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto* thread = new QThread(this);
        auto* worker = new SocketWorker;
        worker->moveToThread(thread);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        thread->start();
        m_threads.append(thread);
        m_workers.append(worker);
    }

    LOG_WARN(QString("Socket worker pool started with %1 thread(s)").arg(count));
}

void SocketWorkerPool::stop()
{
    for (QThread* thread : m_threads) {
        thread->quit();
    }
    for (QThread* thread : m_threads) {
        thread->wait();
    }
    qDeleteAll(m_threads);
    m_threads.clear();
    m_workers.clear();
    m_nextIndex = 0;
}

SocketWorker* SocketWorkerPool::nextWorker()
{
    if (m_workers.isEmpty()) {
        return nullptr;
    }

    SocketWorker* worker = m_workers.at(m_nextIndex);
    m_nextIndex = (m_nextIndex + 1) % m_workers.size();
    return worker;
}

int SocketWorkerPool::workerCount() const
{
    return m_workers.size();
}
