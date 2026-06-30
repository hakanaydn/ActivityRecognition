#include "packet.h"

uint16_t packet_crc16(const void *data, int len)
{
    uint16_t crc = 0xFFFF;
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        crc ^= *p++ << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

void packet_fill(Packet *pkt, uint32_t seq, uint32_t magic,
                 const void *payload, uint8_t plen)
{
    pkt->magic = magic;
    pkt->seq = seq;
    pkt->timestamp = 0;
    for (uint8_t i = 0; i < plen && i < PACKET_PAYLOAD_LEN; i++)
        pkt->payload[i] = ((const uint8_t *)payload)[i];
    pkt->crc = packet_crc16(&pkt->seq, sizeof(Packet) - 6);
}

uint8_t packet_verify(const Packet *pkt)
{
    return (pkt->magic == PACKET_MAGIC_IMU ||
            pkt->magic == PACKET_MAGIC_DEBUG ||
            pkt->magic == PACKET_MAGIC_CMD ||
            pkt->magic == PACKET_MAGIC_ACK) &&
           packet_crc16(&pkt->seq, sizeof(Packet) - 6) == pkt->crc;
}
