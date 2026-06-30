#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define PACKET_MAGIC_IMU   0x494D5550U
#define PACKET_MAGIC_DEBUG 0x44424750U
#define PACKET_MAGIC_CMD   0x434D4430U
#define PACKET_MAGIC_ACK   0x41434B30U
#define PACKET_MAGIC_STAT  0x53544154U
#define PACKET_SIZE         32
#define PACKET_PAYLOAD_LEN  18

#define CMD_CALIBRATE   0x01
#define CMD_RESET       0x02
#define CMD_DEBUG_ON    0x03
#define CMD_DEBUG_OFF   0x04
#define CMD_SERVO           0x05
#define CMD_TEST_DATA_ON    0x06
#define CMD_TEST_DATA_OFF   0x07

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint32_t timestamp;
    uint8_t  payload[PACKET_PAYLOAD_LEN];
    uint16_t crc;
} Packet;

typedef struct __attribute__((packed)) {
    int16_t  ax, ay, az;
    int16_t  gx, gy, gz;
    uint8_t  lpf_enabled;
    uint16_t batt_mv;
    uint8_t  _pad[3];
} IMUPayload;

typedef struct __attribute__((packed)) {
    uint8_t  len;
    char     text[17];
} DebugPayload;

typedef struct __attribute__((packed)) {
    uint8_t  cmd_id;
    uint8_t  params[17];
} CmdPayload;

typedef struct __attribute__((packed)) {
    uint8_t  nrf_link;
    uint8_t  last_rx_sec;
    uint16_t pkt_count;
    uint16_t err_count;
    uint8_t  _pad[12];
} StatusPayload;

uint16_t packet_crc16(const void *data, int len);
void     packet_fill(Packet *pkt, uint32_t seq, uint32_t magic,
                     const void *payload, uint8_t plen);
uint8_t  packet_verify(const Packet *pkt);

#endif
