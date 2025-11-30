#include "packet.h"


QByteArray makePacket(uint32_t type, const QByteArray &body, uint32_t src, uint32_t dest)
{
    QByteArray packet;

    // 填充Header
    PDUHeader header;
    header.total_len = sizeof(PDUHeader) + body.size();
    header.msg_type = type;
    header.src_id = src;
    header.dest_id = dest;

    packet.append((const char*)&header,sizeof(PDUHeader));
    packet.append(body);

    return packet;
}
