#ifndef SOCKETWORKERPOOL_H
#define SOCKETWORKERPOOL_H

#include <QObject>
#include <QVector>

class QThread;
class SocketWorker;

class SocketWorkerPool : public QObject
{
    Q_OBJECT
public:
    explicit SocketWorkerPool(QObject *parent = nullptr);
    ~SocketWorkerPool();

    void start(int workerCount);
    void stop();
    SocketWorker* nextWorker();
    int workerCount() const;

private:
    QVector<QThread*> m_threads;
    QVector<SocketWorker*> m_workers;
    int m_nextIndex = 0;
};

#endif // SOCKETWORKERPOOL_H
