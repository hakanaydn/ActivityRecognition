#include "vcp.h"
#include "stm32f1xx.h"

#define USB_CNTR    (*((volatile uint16_t *)(USB_BASE + 0x00)))
#define USB_ISTR    (*((volatile uint16_t *)(USB_BASE + 0x04)))
#define USB_FNR     (*((volatile uint16_t *)(USB_BASE + 0x0C)))
#define USB_DADDR   (*((volatile uint16_t *)(USB_BASE + 0x14)))
#define USB_BTABLE  (*((volatile uint16_t *)(USB_BASE + 0x18)))
#define USB_EP(n)   (*((volatile uint16_t *)(USB_BASE + 0x1C + (n)*4)))

#define PMA_BASE    0x40006000
#define PMA_HW(o)   (*((volatile uint16_t *)(PMA_BASE + (o))))

#define PMA_TX_ADDR_OFF(ep)  ((ep) * 8 + 0)
#define PMA_TX_CNT_OFF(ep)   ((ep) * 8 + 2)
#define PMA_RX_ADDR_OFF(ep)  ((ep) * 8 + 4)
#define PMA_RX_CNT_OFF(ep)   ((ep) * 8 + 6)

#define ISTR_CTR    (1 << 15)
#define ISTR_DOVR   (1 << 14)
#define ISTR_ERR    (1 << 13)
#define ISTR_WKUP   (1 << 12)
#define ISTR_SUSP   (1 << 11)
#define ISTR_RESET  (1 << 10)
#define ISTR_SOF    (1 << 9)
#define ISTR_ESOF   (1 << 8)

#define USB_FRES    (1 << 0)

/* EPnR register bit positions (ST reference manual) */
#define CTR_RX      0x8000
#define DTOG_RX     0x4000
#define STAT_RX     0x3000
#define SETUP       0x0800
#define EP_TYPE     0x0600
#define EP_KIND     0x0100
#define CTR_TX      0x0080
#define DTOG_TX     0x0040
#define STAT_TX     0x0030
#define EP_CFG      (EP_TYPE | EP_KIND | 0x000F)
#define EP_PRESERVE (CTR_RX | SETUP | CTR_TX | EP_CFG)

#define STAT_DIS    0x00
#define STAT_STALL  0x01
#define STAT_NAK    0x02
#define STAT_VALID  0x03

#define TYPE_BULK   0x0000
#define TYPE_CTRL   0x0200
#define TYPE_ISO    0x0400
#define TYPE_INT    0x0600

#define LINE_CODING_SIZE 7

/* Addresses are byte offsets from PMA_BASE */
#define BTABLE_ADDR   0
#define EP0_TX_ADDR   0x40
#define EP0_RX_ADDR   0x80
#define EP1_TX_ADDR   0xC0
#define EP2_TX_ADDR   0xC8
#define EP3_RX_ADDR   0x108
#define EP3_RX_CNT    0x10C

enum { CONFIG_NONE, CONFIG_SET };

static volatile uint8_t configured;
static volatile uint8_t rx_avail;
static volatile uint8_t rx_buf[VCP_BULK_SIZE];
static volatile uint8_t rx_len;
static uint8_t line_coding[LINE_CODING_SIZE] = {
    0x80, 0x25, 0x00, 0x00,
    0x00,
    0x00,
    0x08
};

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
    0x83, 0x04, 0x40, 0x57, 0x00, 0x01, 0x01, 0x02, 0x00, 1
};

static const uint8_t cfg_desc[] = {
    9, 2, 0x43, 0x00, 2, 1, 0, 0xC0, 50,
    8, 11, 0, 2, 0x02, 0x02, 0x01, 0,
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 1,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 0, 1,
    7, 5, 0x81, 0x03, 8, 0, 0x0A,
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 5, 0x82, 0x02, 64, 0, 0,
    7, 5, 0x03, 0x02, 64, 0, 0,
};

static void pma_write(uint16_t byte_off, const uint8_t *data, int len)
{
    for (int i = 0; i < len; i += 2) {
        uint16_t w = data[i];
        if (i + 1 < len) w |= (uint16_t)data[i + 1] << 8;
        PMA_HW(byte_off + i) = w;
    }
}

static void pma_read(uint16_t byte_off, uint8_t *data, int len)
{
    for (int i = 0; i < len; i += 2) {
        uint16_t w = PMA_HW(byte_off + i);
        data[i] = w & 0xFF;
        if (i + 1 < len) data[i + 1] = (w >> 8) & 0xFF;
    }
}

static uint16_t ep_read_reg(int ep)
{
    return USB_EP(ep);
}

static void ep_write_reg(int ep, uint16_t val)
{
    USB_EP(ep) = val;
}

static void ep_set_stat_tx(int ep, uint16_t stat)
{
    uint16_t r = ep_read_reg(ep);
    uint16_t v = stat & 0x03;
    ep_write_reg(ep, (r & (EP_PRESERVE | STAT_TX)) ^ ((v << 4) & STAT_TX));
}

static void ep_set_stat_rx(int ep, uint16_t stat)
{
    uint16_t r = ep_read_reg(ep);
    uint16_t v = stat & 0x03;
    ep_write_reg(ep, (r & (EP_PRESERVE | STAT_RX)) ^ ((v << 12) & STAT_RX));
}

static void ep_set_tx_addr(int ep, uint16_t byte_off)
{
    uint16_t o = PMA_TX_ADDR_OFF(ep);
    uint16_t word_idx = byte_off >> 1;
    PMA_HW(o) = word_idx & 0xFFFE;
}

static void ep_set_tx_count(int ep, int bytes)
{
    uint16_t words = (bytes + 1) >> 1;
    PMA_HW(PMA_TX_CNT_OFF(ep)) = words & 0x3FF;
}

static void ep_set_rx_buf(int ep, uint16_t byte_off, int size)
{
    uint16_t word_idx = byte_off >> 1;
    PMA_HW(PMA_RX_ADDR_OFF(ep)) = word_idx & 0xFFFE;

    uint16_t num_blocks;
    uint16_t bl_size;
    if (size >= 62) {
        bl_size = 1;
        num_blocks = (size + 31) / 32;
    } else {
        bl_size = 0;
        num_blocks = (size + 1) / 2;
    }
    if (num_blocks > 0) num_blocks--;
    PMA_HW(PMA_RX_CNT_OFF(ep)) = (bl_size << 15) | ((num_blocks & 0x1F) << 10);
}

static uint16_t ep_rx_count(int ep)
{
    uint16_t v = PMA_HW(PMA_RX_CNT_OFF(ep));
    return v & 0x3FF;
}

static void ep0_tx_data(const uint8_t *data, int len)
{
    pma_write(EP0_TX_ADDR, data, len);
    ep_set_tx_count(0, len);
    ep_set_stat_tx(0, STAT_VALID);
}

static void send_zlp(void)
{
    ep_set_tx_count(0, 0);
    ep_set_stat_tx(0, STAT_VALID);
}

static void handle_setup(void)
{
    uint8_t buf[8];
    pma_read(EP0_RX_ADDR, buf, 8);

    uint8_t bmRequestType = buf[0];
    uint8_t bRequest = buf[1];
    uint16_t wValue = buf[2] | (buf[3] << 8);
    uint16_t wIndex = buf[4] | (buf[5] << 8);
    uint16_t wLength = buf[6] | (buf[7] << 8);
    (void)wIndex;

    if ((bmRequestType & 0x60) == 0x20) {
        if (bRequest == 0x20 && wLength == LINE_CODING_SIZE) {
            ep_set_stat_rx(0, STAT_VALID);
            return;
        }
        if (bRequest == 0x21) {
            ep0_tx_data(line_coding, LINE_CODING_SIZE);
            return;
        }
        if (bRequest == 0x22) {
            send_zlp();
            ep_set_stat_rx(0, STAT_VALID);
            return;
        }
        if (bRequest == 0x23) {
            send_zlp();
            return;
        }
        ep_set_stat_tx(0, STAT_STALL);
        return;
    }

    if (bmRequestType == 0x80 && bRequest == 6) {
        uint8_t desc_type = (wValue >> 8) & 0xFF;
        const uint8_t *desc;
        int len;

        if (desc_type == 1) { desc = dev_desc; len = 18; }
        else if (desc_type == 2) { desc = cfg_desc; len = sizeof(cfg_desc); }
        else { ep_set_stat_tx(0, STAT_STALL); return; }

        if (wLength < (uint16_t)len) len = wLength;
        ep0_tx_data(desc, len);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 5) {
        USB_DADDR = (wValue & 0x7F) | 0x80;
        send_zlp();
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 9) {
        if (wValue == 1) {
            configured = CONFIG_SET;
            ep_set_stat_tx(1, STAT_NAK);
            ep_set_stat_tx(2, STAT_NAK);
            ep_set_stat_rx(3, STAT_VALID);
        }
        send_zlp();
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 0) {
        configured = CONFIG_NONE;
        send_zlp();
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    ep_set_stat_tx(0, STAT_STALL);
}

static void handle_ep0_out(void)
{
    uint16_t len = ep_rx_count(0);
    if (len > LINE_CODING_SIZE) len = LINE_CODING_SIZE;
    pma_read(EP0_RX_ADDR, (uint8_t *)line_coding, len);
    send_zlp();
    ep_set_stat_rx(0, STAT_VALID);
}

static void handle_ctr(void)
{
    uint16_t istr = USB_ISTR;
    int ep = istr & 0x0F;
    uint16_t reg = ep_read_reg(ep);

    if (ep == 0) {
        if (reg & CTR_RX) {
            if (reg & SETUP) {
                handle_setup();
            } else {
                handle_ep0_out();
            }
            return;
        }
        if (reg & CTR_TX) {
            ep_set_stat_rx(0, STAT_VALID);
            return;
        }
        return;
    }

    if (ep == 1 && (reg & CTR_TX)) {
        return;
    }

    if (ep == 2 && (reg & CTR_TX)) {
        return;
    }

    if (ep == 3 && (reg & CTR_RX)) {
        rx_len = ep_rx_count(3);
        if (rx_len > VCP_BULK_SIZE) rx_len = VCP_BULK_SIZE;
        pma_read(EP3_RX_ADDR, (uint8_t *)rx_buf, rx_len);
        rx_avail = 1;
        ep_set_stat_rx(3, STAT_VALID);
        return;
    }
}

void VCP_Init(int pll48)
{
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;

    if (pll48) {
        RCC->CFGR |= RCC_CFGR_USBPRE;
    } else {
        RCC->CFGR &= ~RCC_CFGR_USBPRE;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    USB_CNTR = USB_FRES;
    for (volatile int i = 0; i < 1000; i++);
    USB_BTABLE = BTABLE_ADDR;
    USB_DADDR = 0;
    USB_ISTR = 0;
    USB_CNTR = 0;

    ep_set_tx_addr(0, EP0_TX_ADDR);
    ep_set_tx_count(0, 0);
    ep_set_rx_buf(0, EP0_RX_ADDR, 64);

    ep_write_reg(0, 0 | TYPE_CTRL);
    ep_set_stat_tx(0, STAT_NAK);
    ep_set_stat_rx(0, STAT_VALID);

    ep_set_tx_addr(1, EP1_TX_ADDR);
    ep_set_tx_count(1, 0);
    ep_write_reg(1, 1 | TYPE_INT);
    ep_set_stat_tx(1, STAT_NAK);

    ep_set_tx_addr(2, EP2_TX_ADDR);
    ep_set_tx_count(2, 0);
    ep_write_reg(2, 2 | TYPE_BULK);

    ep_set_rx_buf(3, EP3_RX_ADDR, 64);
    ep_write_reg(3, 3 | TYPE_BULK);
    ep_set_stat_rx(3, STAT_VALID);

    configured = CONFIG_NONE;
    rx_avail = 0;

    USB_DADDR = 0x80;
}

void VCP_Poll(void)
{
    uint16_t istr = USB_ISTR;

    if (istr & ISTR_RESET) {
        configured = CONFIG_NONE;
        while (USB_ISTR & ISTR_RESET) {
            USB_ISTR = ISTR_RESET;
        }
        USB_DADDR = 0x80;
        ep_set_stat_tx(0, STAT_NAK);
        ep_set_stat_rx(0, STAT_VALID);
        ep_set_stat_tx(1, STAT_DIS);
        ep_set_stat_tx(2, STAT_DIS);
        ep_set_stat_rx(3, STAT_VALID);
        return;
    }

    if (istr & ISTR_CTR) {
        do {
            handle_ctr();
        } while (USB_ISTR & ISTR_CTR);
        return;
    }

    if (istr) {
        USB_ISTR = istr;
    }
}

uint8_t VCP_IsConfigured(void)
{
    return configured;
}

uint8_t VCP_RxAvail(void)
{
    return rx_avail;
}

uint8_t VCP_Read(uint8_t *data, uint8_t *len)
{
    if (!rx_avail) return 0;
    *len = rx_len;
    for (uint8_t i = 0; i < rx_len; i++) data[i] = rx_buf[i];
    rx_avail = 0;
    return 1;
}
