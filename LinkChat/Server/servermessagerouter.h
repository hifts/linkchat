#ifndef SERVERMESSAGEROUTER_H
#define SERVERMESSAGEROUTER_H

#include <QByteArray>
#include <cstdint>

class ClientSocket;

class ServerMessageRouter
{
public:
    explicit ServerMessageRouter(ClientSocket* socket);

    void dispatch(uint32_t msgType, uint32_t srcId, uint32_t destId, const QByteArray& bodyData);

private:
    ClientSocket* m_socket;
};

#endif // SERVERMESSAGEROUTER_H
