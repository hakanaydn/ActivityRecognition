#include "nrf24.h"
#include "packet.h"
#include "uart_com.h"
#include "nrf24_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

uint8_t nrf24_init_ok = 0;


static Packet cmd_pkt;
static uint32_t last_rx_tick;
static uint16_t stat_pkt_count;
static uint16_t stat_err_count;
static uint8_t dummy_enabled;
static uint32_t dummy_seq;

static const int16_t sin_tbl[32] = {
    0, 6393, 12543, 18205, 23170, 27246, 30274, 32138,
    32767, 32138, 30274, 27246, 23170, 18205, 12543, 6393,
    0, -6393, -12543, -18205, -23170, -27246, -30274, -32138,
    -32767, -32138, -30274, -27246, -23170, -18205, -12543, -6393
};

void NRF24Task(void *params)
{
    (void)params;
    last_rx_tick = 0;
    stat_pkt_count = 0;
    stat_err_count = 0;
    dummy_enabled = 0;
    dummy_seq = 0;

    vTaskDelay(pdMS_TO_TICKS(100));

    for (;;) {
        uint8_t data[NRF24_MAX_PAYLOAD];
        uint8_t len;
        if (NRF24_Receive(data, &len)) {
            if (len == sizeof(Packet)) {
                const Packet *p = (const Packet *)data;
                if (packet_verify(p)) {
                    stat_pkt_count++;
                    last_rx_tick = xTaskGetTickCount();
                    UART_Com_Send((const uint8_t *)p, sizeof(Packet));
                } else {
                    stat_err_count++;
                }
            }
            NRF24_SendAckPayload((const uint8_t *)&cmd_pkt, sizeof(Packet));
        }

        Packet rx_pkt;
        if (UART_Com_ReadPacket(&rx_pkt)) {
            if (rx_pkt.magic == PACKET_MAGIC_CMD) {
                CmdPayload *cp = (CmdPayload *)rx_pkt.payload;
                if (cp->cmd_id == CMD_TEST_DATA_ON) {
                    dummy_enabled = 1;
                    dummy_seq = 0;
                } else if (cp->cmd_id == CMD_TEST_DATA_OFF) {
                    dummy_enabled = 0;
                } else {
                    uint8_t ack_buf[sizeof(cmd_pkt)];
                    uint8_t ack_len = sizeof(cmd_pkt);
                    cmd_pkt = rx_pkt;
                    NRF24_TransmitAck((const uint8_t *)&rx_pkt, sizeof(Packet),
                                      ack_buf, &ack_len, 100);
                }
            }
        }

        static TickType_t last_status = 0;
        TickType_t now = xTaskGetTickCount();
        if (now - last_status > pdMS_TO_TICKS(2000)) {
            last_status = now;
            TickType_t age = (last_rx_tick > 0) ? (now - last_rx_tick) : 0;
            StatusPayload sp;
            sp.nrf_link = (last_rx_tick > 0 && age < pdMS_TO_TICKS(5000)) ? 2 : 1;
            sp.last_rx_sec = (uint8_t)(age / 1000);
            sp.pkt_count = stat_pkt_count;
            sp.err_count = stat_err_count;
            memset(sp._pad, 0, 12);
            Packet spkt;
            packet_fill(&spkt, 0, PACKET_MAGIC_STAT, (const uint8_t *)&sp, sizeof(sp));
            UART_Com_Send((const uint8_t *)&spkt, sizeof(Packet));
        }

        if (dummy_enabled) {
            uint32_t s = dummy_seq;
            int16_t a = sin_tbl[(s * 3) & 0x1F];
            int16_t b = sin_tbl[(s * 2 + 6) & 0x1F];
            int16_t c = sin_tbl[(s * 1 + 12) & 0x1F];
            int16_t d = sin_tbl[(s * 4 + 3) & 0x1F];
            int16_t e = sin_tbl[(s * 5 + 9) & 0x1F];
            int16_t f = sin_tbl[(s * 6 + 15) & 0x1F];
            dummy_seq++;

            IMUPayload imu;
            imu.ax = a >> 1; imu.ay = b >> 1; imu.az = (c >> 2) + 8192;
            imu.gx = (int16_t)((int32_t)d * 100 / 327);
            imu.gy = (int16_t)((int32_t)e * 80 / 327);
            imu.gz = (int16_t)((int32_t)f * 60 / 327);
            imu.lpf_enabled = 0;
            imu.batt_mv = 3300;
            for (int i = 0; i < 3; i++) imu._pad[i] = 0;

            Packet pkt;
            packet_fill(&pkt, dummy_seq, PACKET_MAGIC_IMU,
                        (const uint8_t *)&imu, sizeof(imu));
            UART_Com_Send((const uint8_t *)&pkt, sizeof(Packet));
            stat_pkt_count++;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        vTaskDelay(1);
    }
}
