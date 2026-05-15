#ifndef SOCKETWORKER_H
#define SOCKETWORKER_H

#include <QObject>

class SocketWorker : public QObject
{
    Q_OBJECT
public:
    explicit SocketWorker(QObject *parent = nullptr);

public slots:
    void addConnection(qintptr socketDescriptor);
};

#endif // SOCKETWORKER_H
