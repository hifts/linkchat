#include "packetcodec.h"

#include <QtGlobal>
#include <cstring>

PacketCodec::PacketCodec(uint32_t maxNormalPacketLen, uint32_t maxFilePacketLen)
    : m_maxNormalPacketLen(maxNormalPacketLen)
    , m_maxFilePacketLen(maxFilePacketLen)
{
}

PacketReadStatus PacketCodec::takeNextPacket(QByteArray& buffer, DecodedPacket& packet, QString* error) const
{
    while (buffer.size() >= static_cast<int>(sizeof(uint32_t))) {
        uint32_t magic = 0;
        std::memcpy(&magic, buffer.constData(), sizeof(uint32_t));
        if (magic == PDU_MAGIC) {
            break;
        }

        int nextIdx = -1;
        for (int i = 1; i <= buffer.size() - static_cast<int>(sizeof(uint32_t)); ++i) {
            uint32_t candidate = 0;
            std::memcpy(&candidate, buffer.constData() + i, sizeof(uint32_t));
            if (candidate == PDU_MAGIC) {
                nextIdx = i;
                break;
            }
        }

        if (nextIdx > 0) {
            buffer.remove(0, nextIdx);
        } else {
            const int keep = qMin(3, buffer.size());
            buffer.remove(0, buffer.size() - keep);
            return PacketReadStatus::NeedMoreData;
        }
    }

    if (buffer.size() < static_cast<int>(sizeof(PDUHeader))) {
        return PacketReadStatus::NeedMoreData;
    }

    PDUHeader header;
    std::memcpy(&header, buffer.constData(), sizeof(PDUHeader));

    const uint32_t maxLen = maxPacketLen(header.msg_type);
    if (header.total_len < sizeof(PDUHeader) || header.total_len > maxLen) {
        if (error) {
            *error = QString("invalid packet length: %1").arg(header.total_len);
        }
        buffer.remove(0, sizeof(uint32_t));
        return PacketReadStatus::ProtocolError;
    }

    if (buffer.size() < static_cast<int>(header.total_len)) {
        return PacketReadStatus::NeedMoreData;
    }

    packet.header = header;
    packet.body = buffer.mid(sizeof(PDUHeader), header.total_len - sizeof(PDUHeader));
    buffer.remove(0, header.total_len);
    return PacketReadStatus::PacketReady;
}

uint32_t PacketCodec::maxPacketLen(uint32_t msgType) const
{
    return isFilePacket(msgType) ? m_maxFilePacketLen : m_maxNormalPacketLen;
}

bool PacketCodec::isFilePacket(uint32_t msgType) const
{
    return msgType == MSG_FILE_CHUNK;
}
