#ifndef CLIENTMESSAGEROUTER_H
#define CLIENTMESSAGEROUTER_H

#include <QByteArray>
#include <cstdint>

class NetworkManager;

class ClientMessageRouter
{
public:
    explicit ClientMessageRouter(NetworkManager* manager);

    void dispatch(uint32_t msgType, uint32_t srcId, const QByteArray& body);

private:
    NetworkManager* m_manager;
};

#endif // CLIENTMESSAGEROUTER_H
