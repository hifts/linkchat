#include "packet.h"


QByteArray makePacket(uint32_t type, const QByteArray &body, uint32_t src, uint32_t dest)
{
    QByteArray packet;
    packet.reserve(sizeof(PDUHeader) + body.size());

    PDUHeader header;
    header.magic     = PDU_MAGIC;
    header.total_len = sizeof(PDUHeader) + body.size();
    header.msg_type  = type;
    header.src_id    = src;
    header.dest_id   = dest;

    packet.append((const char*)&header,sizeof(PDUHeader));
    packet.append(body);

    return packet;
}

QByteArray makePacketFromParts(uint32_t type,
                               const char *bodyHead,
                               qsizetype bodyHeadSize,
                               const QByteArray &bodyTail,
                               uint32_t src,
                               uint32_t dest)
{
    const qsizetype bodySize = bodyHeadSize + bodyTail.size();
    QByteArray packet;
    packet.reserve(sizeof(PDUHeader) + bodySize);

    PDUHeader header;
    header.magic     = PDU_MAGIC;
    header.total_len = sizeof(PDUHeader) + bodySize;
    header.msg_type  = type;
    header.src_id    = src;
    header.dest_id   = dest;

    packet.append(reinterpret_cast<const char*>(&header), sizeof(PDUHeader));
    if (bodyHead && bodyHeadSize > 0) {
        packet.append(bodyHead, bodyHeadSize);
    }
    packet.append(bodyTail);

    return packet;
}
