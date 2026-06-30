#include "usb_bulk.h"
#include "stm32f1xx.h"

#define USB_CNTR    (*((volatile uint16_t *)(USB_BASE + 0x00)))
#define USB_ISTR    (*((volatile uint16_t *)(USB_BASE + 0x04)))
#define USB_FNR     (*((volatile uint16_t *)(USB_BASE + 0x0C)))
#define USB_DADDR   (*((volatile uint16_t *)(USB_BASE + 0x14)))
#define USB_BTABLE  (*((volatile uint16_t *)(USB_BASE + 0x18)))
#define USB_EP(n)   (*((volatile uint16_t *)(USB_BASE + 0x1C + (n)*4)))

#define PMA_ADDR    0x40006000
#define PMA_WORD(o) (*((volatile uint16_t *)(PMA_ADDR + (o))))

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

#define EP_STAT_TX  0x0030
#define EP_STAT_RX  0x3000
#define STAT_DIS    0x0000
#define STAT_STALL  0x0010
#define STAT_NAK    0x0020
#define STAT_VALID  0x0030
#define EP_TYPE     0x0300
#define TYPE_BULK   0x0000
#define TYPE_CTRL   0x0100
#define TYPE_ISO    0x0200
#define TYPE_INT    0x0300
#define EP_KIND     0x0400
#define CTR_RX      0x8000
#define CTR_TX      0x0080
#define DTOG_TX     0x0040
#define DTOG_RX     0x4000
#define SETUP       0x0800

#define BTABLE_ADDR 0
#define EP0_TX_ADDR 0x40
#define EP0_RX_ADDR 0x80
#define EP1_TX_ADDR 0xC0
#define EP2_RX_ADDR 0x100

#define DESC_SIZE   18

enum { CONFIG_NONE, CONFIG_SET };

static volatile uint8_t configured;
static volatile uint8_t ep1_busy;
static volatile uint8_t rx_avail;
static volatile uint8_t rx_buf[USB_BULK_SIZE];
static volatile uint8_t rx_len;
static volatile uint8_t tx_buf[USB_BULK_SIZE];
static volatile uint8_t tx_len;
static volatile uint8_t tx_pending;

static const uint8_t dev_desc[DESC_SIZE] = {
    18, 1, 0x00, 0x02, 0xFF, 0, 0, 64,
    0x83, 0x04, 0x11, 0x57, 0x00, 0x01, 0x01, 0x02, 0, 1
};

static const uint8_t cfg_desc[] = {
    9, 2, 32, 0, 1, 0, 0, 0xC0, 50,
    9, 4, 0, 0, 2, 0xFF, 0, 0, 0,
    7, 5, USB_BULK_EP_IN, 2, 64, 0, 0,
    7, 5, USB_BULK_EP_OUT, 2, 64, 0, 0,
};

static void pma_write(uint16_t addr, const uint8_t *data, int len)
{
    for (int i = 0; i < len; i += 2) {
        uint16_t w = data[i];
        if (i + 1 < len) w |= (uint16_t)data[i + 1] << 8;
        PMA_WORD(addr + i) = w;
    }
}

static void pma_read(uint16_t addr, uint8_t *data, int len)
{
    for (int i = 0; i < len; i += 2) {
        uint16_t w = PMA_WORD(addr + i);
        data[i] = w & 0xFF;
        if (i + 1 < len) data[i + 1] = (w >> 8) & 0xFF;
    }
}

static void ep_set_tx_addr(int ep, uint16_t addr)
{
    uint16_t o = PMA_TX_ADDR_OFF(ep);
    uint16_t v = PMA_WORD(o);
    PMA_WORD(o) = (v & 1) | ((addr >> 1) << 1);
}

static void ep_set_tx_count(int ep, int count)
{
    uint16_t o = PMA_TX_ADDR_OFF(ep);
    uint16_t v = PMA_WORD(o);
    PMA_WORD(o) = (v & 0xFFFE) | (count & 1);
    PMA_WORD(PMA_TX_CNT_OFF(ep)) = (count >> 1) & 0x3FF;
}

static void ep_set_rx_buf(int ep, uint16_t addr, int size)
{
    uint16_t num_blocks = (size / 32) - 1;
    uint16_t o = PMA_RX_ADDR_OFF(ep);
    PMA_WORD(o) = ((addr >> 1) << 5) | ((num_blocks & 0x0F) << 1);
}

static uint16_t ep_rx_count(int ep)
{
    uint16_t v = PMA_WORD(PMA_RX_CNT_OFF(ep));
    return v & 0x3FF;
}

static void ep_write_reg(int ep, uint16_t val)
{
    USB_EP(ep) = val;
}

static uint16_t ep_read_reg(int ep)
{
    return USB_EP(ep);
}

static void ep_set_stat_tx(int ep, uint16_t stat)
{
    uint16_t r = ep_read_reg(ep);
    r &= ~EP_STAT_TX;
    r |= stat;
    ep_write_reg(ep, r);
}

static void ep_set_stat_rx(int ep, uint16_t stat)
{
    uint16_t r = ep_read_reg(ep);
    r &= ~EP_STAT_RX;
    r |= (stat << 8) & EP_STAT_RX;
    ep_write_reg(ep, r);
}

static void ep0_tx_data(const uint8_t *data, int len)
{
    pma_write(EP0_TX_ADDR, data, len);
    ep_set_tx_count(0, len);
    ep_set_stat_tx(0, STAT_VALID);
}

static void handle_setup(void)
{
    uint8_t buf[8];
    pma_read(EP0_RX_ADDR, buf, 8);

    uint8_t bmRequestType = buf[0];
    uint8_t bRequest = buf[1];
    uint16_t wValue = buf[2] | (buf[3] << 8);
    uint16_t wLength = buf[6] | (buf[7] << 8);
    (void)wValue;

    if (bmRequestType == 0x80 && bRequest == 6) {
        uint8_t desc_type = (wValue >> 8) & 0xFF;
        uint8_t desc_idx = wValue & 0xFF;
        const uint8_t *desc;
        int len;

        if (desc_type == 1) { desc = dev_desc; len = DESC_SIZE; }
        else if (desc_type == 2) { desc = cfg_desc; len = sizeof(cfg_desc); }
        else { ep_set_stat_tx(0, STAT_STALL); return; }

        (void)desc_idx;
        if (wLength < (uint16_t)len) len = wLength;
        ep0_tx_data(desc, len);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 5) {
        USB_DADDR = (wValue & 0x7F) | 0x80;
        ep_set_stat_tx(0, STAT_VALID);
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 9) {
        if (wValue == 1) {
            configured = CONFIG_SET;
            ep_set_stat_tx(1, STAT_NAK);
            ep_set_stat_rx(2, STAT_VALID);
        }
        ep_set_stat_tx(0, STAT_VALID);
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    if (bmRequestType == 0x00 && bRequest == 0) {
        ep_set_stat_tx(0, STAT_VALID);
        ep_set_stat_rx(0, STAT_VALID);
        return;
    }

    ep_set_stat_tx(0, STAT_STALL);
}

static void handle_ctr(void)
{
    uint16_t irq = USB_ISTR;
    int ep = irq & 0x0F;
    uint16_t reg = ep_read_reg(ep);

    if (ep == 0) {
        if (reg & CTR_RX) {
            reg &= ~CTR_RX;
            ep_write_reg(0, reg);
            if (reg & SETUP) {
                handle_setup();
            } else {
                ep_set_stat_rx(0, STAT_VALID);
            }
            return;
        }
        if (reg & CTR_TX) {
            reg &= ~CTR_TX;
            ep_write_reg(0, reg);
            ep_set_stat_rx(0, STAT_VALID);
            return;
        }
    }

    if (ep == 1 && (reg & CTR_TX)) {
        reg &= ~CTR_TX;
        ep_write_reg(1, reg);
        ep1_busy = 0;
        if (tx_pending) {
            tx_pending = 0;
            uint16_t o = PMA_TX_ADDR_OFF(1);
            uint16_t v = PMA_WORD(o);
            PMA_WORD(o) = (v & 1) | ((EP1_TX_ADDR >> 1) << 1);
            pma_write(EP1_TX_ADDR, (const uint8_t *)tx_buf, tx_len);
            ep_set_tx_count(1, tx_len);
            ep_set_stat_tx(1, STAT_VALID);
        }
        return;
    }

    if (ep == 2 && (reg & CTR_RX)) {
        reg &= ~CTR_RX;
        ep_write_reg(2, reg);
        rx_len = ep_rx_count(2) & 0x3F;
        pma_read(EP2_RX_ADDR, (uint8_t *)rx_buf, rx_len);
        rx_avail = 1;
        ep_set_stat_rx(2, STAT_VALID);
        return;
    }
}

void USB_Bulk_Init_Poll(int pll48)
{
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    if (pll48) {
        RCC->CFGR |= RCC_CFGR_USBPRE;
    } else {
        RCC->CFGR &= ~RCC_CFGR_USBPRE;
    }

    GPIOA->CRH = (GPIOA->CRH & 0xFF00FFFF) | 0x00BB0000;
    for (volatile int i = 0; i < 500000; i++);

    USB_CNTR |= USB_FRES;
    for (volatile int i = 0; i < 1000; i++);
    USB_CNTR &= ~USB_FRES;
    USB_BTABLE = BTABLE_ADDR;

    ep_set_tx_addr(0, EP0_TX_ADDR);
    ep_set_tx_count(0, 0);
    ep_set_rx_buf(0, EP0_RX_ADDR, 64);

    ep_write_reg(0, 0 | TYPE_CTRL);
    ep_set_stat_tx(0, STAT_NAK);
    ep_set_stat_rx(0, STAT_VALID);

    ep_set_tx_addr(1, EP1_TX_ADDR);
    ep_set_tx_count(1, 0);
    ep_write_reg(1, 1 | TYPE_BULK);

    ep_set_rx_buf(2, EP2_RX_ADDR, 64);
    ep_write_reg(2, 2 | TYPE_BULK);
    ep_set_stat_rx(2, STAT_VALID);

    configured = CONFIG_NONE;
    ep1_busy = 0;
    rx_avail = 0;
    tx_pending = 0;

    USB_DADDR = 0x80;
}

void USB_Bulk_Poll(void)
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
        ep_set_stat_rx(1, STAT_DIS);
        ep_set_stat_tx(2, STAT_DIS);
        ep_set_stat_rx(2, STAT_VALID);
        return;
    }

    if (istr & ISTR_CTR) {
        handle_ctr();
        USB_ISTR = ISTR_CTR;
        return;
    }

    if (istr) {
        USB_ISTR = istr;
    }
}

uint8_t USB_Bulk_IsConfigured(void)
{
    return configured;
}
