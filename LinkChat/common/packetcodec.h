#ifndef PACKETCODEC_H
#define PACKETCODEC_H

#include "packet.h"

#include <QByteArray>
#include <QString>

struct DecodedPacket
{
    PDUHeader header;
    QByteArray body;
};

enum class PacketReadStatus
{
    PacketReady,
    NeedMoreData,
    ProtocolError
};

class PacketCodec
{
public:
    PacketCodec(uint32_t maxNormalPacketLen, uint32_t maxFilePacketLen);

    PacketReadStatus takeNextPacket(QByteArray& buffer, DecodedPacket& packet, QString* error = nullptr) const;

private:
    uint32_t maxPacketLen(uint32_t msgType) const;
    bool isFilePacket(uint32_t msgType) const;

    uint32_t m_maxNormalPacketLen;
    uint32_t m_maxFilePacketLen;
};

#endif // PACKETCODEC_H
