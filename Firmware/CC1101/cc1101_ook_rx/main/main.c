/*
 * CC1101 ASK/OOK packet receiver for ESP32 (ESP-IDF)
 * ----------------------------------------------------
 * Wiring (default pins, change in the #defines below if needed):
 *
 *   CC1101 pin   ESP32 pin      Notes
 *   ----------   ---------      -----
 *   VCC          3V3            CC1101 is 3.3V only
 *   GND          GND
 *   SCK          GPIO18         SPI clock
 *   MOSI         GPIO23         SPI MOSI
 *   GDO1         GPIO19         Doubles as MISO/SO in SPI mode
 *   CSN          GPIO5          Chip select
 *   GDO0         GPIO4          Packet-received interrupt line
 *   GDO2         (not used)     Leave unconnected or wire to a spare GPIO
 *
 * Behavior:
 *  - Configures the CC1101 for ASK/OOK modulation at 433.92 MHz, 2400 baud,
 *    fixed packet length (default 4 payload bytes), no CRC, no whitening.
 *  - GDO0 is configured to go high when a sync word is detected and go low
 *    when the full packet has been received (IOCFG0 = 0x06). ESP32 catches
 *    the falling edge via GPIO interrupt and pulls the packet out of the
 *    RX FIFO over SPI.
 *  - Prints the received bytes (hex + ASCII) plus RSSI/LQI to the console.
 *
 * The packet format expected over the air is:
 *   [4x 0xAA preamble][0xD3 0x91 sync word][N payload bytes]
 * This matches the GNU Radio OOK transmit chain described separately.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "CC1101_OOK";

/* ---------------- Pin configuration ---------------- */
#define PIN_SCK   18
#define PIN_MOSI  23
#define PIN_MISO  19   /* physically wired to CC1101 GDO1 */
#define PIN_CSN   5
#define PIN_GDO0  4    /* packet-received interrupt */

#define SPI_HOST_USED SPI2_HOST

/* ---------------- CC1101 register addresses ---------------- */
#define CC1101_IOCFG2     0x00
#define CC1101_IOCFG1     0x01
#define CC1101_IOCFG0     0x02
#define CC1101_FIFOTHR    0x03
#define CC1101_SYNC1      0x04
#define CC1101_SYNC0      0x05
#define CC1101_PKTLEN     0x06
#define CC1101_PKTCTRL1   0x07
#define CC1101_PKTCTRL0   0x08
#define CC1101_ADDR       0x09
#define CC1101_CHANNR     0x0A
#define CC1101_FSCTRL1    0x0B
#define CC1101_FSCTRL0    0x0C
#define CC1101_FREQ2      0x0D
#define CC1101_FREQ1      0x0E
#define CC1101_FREQ0      0x0F
#define CC1101_MDMCFG4    0x10
#define CC1101_MDMCFG3    0x11
#define CC1101_MDMCFG2    0x12
#define CC1101_MDMCFG1    0x13
#define CC1101_MDMCFG0    0x14
#define CC1101_DEVIATN    0x15
#define CC1101_MCSM2      0x16
#define CC1101_MCSM1      0x17
#define CC1101_MCSM0      0x18
#define CC1101_FOCCFG     0x19
#define CC1101_BSCFG      0x1A
#define CC1101_AGCCTRL2   0x1B
#define CC1101_AGCCTRL1   0x1C
#define CC1101_AGCCTRL0   0x1D
#define CC1101_FREND1     0x21
#define CC1101_FREND0     0x22
#define CC1101_FSCAL3     0x23
#define CC1101_FSCAL2     0x24
#define CC1101_FSCAL1     0x25
#define CC1101_FSCAL0     0x26
#define CC1101_TEST2      0x2C
#define CC1101_TEST1      0x2D
#define CC1101_TEST0      0x2E

#define CC1101_PATABLE    0x3E
#define CC1101_TXFIFO     0x3F
#define CC1101_RXFIFO     0x3F

/* Status registers (read with burst bit set) */
#define CC1101_PKTSTATUS  0x38
#define CC1101_RXBYTES    0x3B

/* Strobe commands */
#define CC1101_SRES    0x30
#define CC1101_SFSTXON 0x31
#define CC1101_SXOFF   0x32
#define CC1101_SCAL    0x33
#define CC1101_SRX     0x34
#define CC1101_STX     0x35
#define CC1101_SIDLE   0x36
#define CC1101_SFRX    0x3A
#define CC1101_SFTX    0x3B
#define CC1101_SNOP    0x3D

#define READ_BIT   0x80
#define BURST_BIT  0x40

/* Payload length expected per packet (must match GNU Radio TX side) */
#define PKT_PAYLOAD_LEN 4

static spi_device_handle_t spi;
static QueueHandle_t gdo0_evt_queue;

/* ---------------- Low level SPI helpers ---------------- */

static inline void cc1101_cs_select(void)
{
    gpio_set_level(PIN_CSN, 0);
    /* CC1101 requirement: after CSN goes low, wait until SO (MISO/GDO1)
     * goes low, which signals the crystal oscillator is stable and the
     * chip is ready to accept the SPI header byte. */
    while (gpio_get_level(PIN_MISO) != 0) {
        esp_rom_delay_us(1);
    }
}

static inline void cc1101_cs_deselect(void)
{
    gpio_set_level(PIN_CSN, 1);
}

static uint8_t cc1101_strobe(uint8_t cmd)
{
    uint8_t status = 0;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .rx_buffer = &status,
    };
    cc1101_cs_select();
    spi_device_polling_transmit(spi, &t);
    cc1101_cs_deselect();
    return status;
}

static void cc1101_write_reg(uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = { addr, value };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = NULL,
    };
    cc1101_cs_select();
    spi_device_polling_transmit(spi, &t);
    cc1101_cs_deselect();
}

static uint8_t cc1101_read_reg(uint8_t addr)
{
    uint8_t tx[2] = { (uint8_t)(addr | READ_BIT), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    cc1101_cs_select();
    spi_device_polling_transmit(spi, &t);
    cc1101_cs_deselect();
    return rx[1];
}

static uint8_t cc1101_read_status_reg(uint8_t addr)
{
    /* Status registers require the burst bit set even for single reads */
    uint8_t tx[2] = { (uint8_t)(addr | READ_BIT | BURST_BIT), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    cc1101_cs_select();
    spi_device_polling_transmit(spi, &t);
    cc1101_cs_deselect();
    return rx[1];
}

static void cc1101_read_burst(uint8_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t header = addr | READ_BIT | BURST_BIT;
    cc1101_cs_select();

    spi_transaction_t t_header = {
        .length = 8,
        .tx_buffer = &header,
        .rx_buffer = NULL,
    };
    spi_device_polling_transmit(spi, &t_header);

    spi_transaction_t t_data = {
        .length = (size_t)len * 8,
        .tx_buffer = NULL,
        .rx_buffer = buf,
    };
    /* tx_buffer NULL means MOSI stays low; we only care about rx here */
    static uint8_t dummy_tx[64];
    memset(dummy_tx, 0, sizeof(dummy_tx));
    t_data.tx_buffer = dummy_tx;
    spi_device_polling_transmit(spi, &t_data);

    cc1101_cs_deselect();
}

static void cc1101_reset(void)
{
    gpio_set_level(PIN_CSN, 1);
    esp_rom_delay_us(5);
    gpio_set_level(PIN_CSN, 0);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_CSN, 1);
    esp_rom_delay_us(41);
    cc1101_strobe(CC1101_SRES);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* ---------------- Register configuration table ----------------
 * ASK/OOK, 433.92 MHz, ~2400 baud, 26 MHz crystal.
 * FREQ2:FREQ1:FREQ0 computed as round(433.92e6 * 2^16 / 26e6) = 0x10B071
 * DRATE computed for M=131, E=6 -> ~2399.9 baud
 * Channel filter bandwidth set to 325 kHz (CHANBW_E=1, CHANBW_M=1)
 */
typedef struct {
    uint8_t addr;
    uint8_t value;
} cc1101_reg_t;

static const cc1101_reg_t cc1101_config[] = {
    { CC1101_IOCFG0,   0x06 }, /* GDO0: assert on sync, deassert on packet end */
    { CC1101_FIFOTHR,  0x47 },
    { CC1101_SYNC1,    0xD3 },
    { CC1101_SYNC0,    0x91 },
    { CC1101_PKTLEN,   PKT_PAYLOAD_LEN },
    { CC1101_PKTCTRL1, 0x04 }, /* append RSSI+LQI status bytes, no addr check */
    { CC1101_PKTCTRL0, 0x00 }, /* fixed length, CRC disabled, no whitening */
    { CC1101_FSCTRL1,  0x06 },
    { CC1101_FSCTRL0,  0x00 },
    { CC1101_FREQ2,    0x10 },
    { CC1101_FREQ1,    0xB0 },
    { CC1101_FREQ0,    0x71 },
    { CC1101_MDMCFG4,  0x56 }, /* CHANBW=325kHz, DRATE_E=6 */
    { CC1101_MDMCFG3,  0x83 }, /* DRATE_M=131 -> ~2400 baud */
    { CC1101_MDMCFG2,  0x32 }, /* ASK/OOK, 16/16 sync word bits */
    { CC1101_MDMCFG1,  0x20 }, /* 4-byte preamble, no FEC */
    { CC1101_MDMCFG0,  0x00 },
    { CC1101_DEVIATN,  0x00 }, /* unused for OOK */
    { CC1101_MCSM1,    0x0C }, /* stay in RX after packet received */
    { CC1101_MCSM0,    0x18 }, /* auto-calibrate IDLE->RX/TX */
    { CC1101_FOCCFG,   0x16 },
    { CC1101_BSCFG,    0x6C },
    { CC1101_AGCCTRL2, 0x43 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
    { CC1101_FREND1,   0x56 },
    { CC1101_FREND0,   0x11 },
    { CC1101_FSCAL3,   0xE9 },
    { CC1101_FSCAL2,   0x2A },
    { CC1101_FSCAL1,   0x00 },
    { CC1101_FSCAL0,   0x1F },
    { CC1101_TEST2,    0x81 },
    { CC1101_TEST1,    0x35 },
    { CC1101_TEST0,    0x09 },
};

static void cc1101_configure(void)
{
    for (size_t i = 0; i < sizeof(cc1101_config) / sizeof(cc1101_config[0]); i++) {
        cc1101_write_reg(cc1101_config[i].addr, cc1101_config[i].value);
    }
}

/* ---------------- GDO0 interrupt handling ---------------- */

static void IRAM_ATTR gdo0_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t hp_woken = pdFALSE;
    xQueueSendFromISR(gdo0_evt_queue, &gpio_num, &hp_woken);
    if (hp_woken) portYIELD_FROM_ISR();
}

static void print_packet(const uint8_t *buf, size_t len)
{
    printf("\n===== DATA PACKET RECEIVED =====\n");
    printf("Length: %d bytes\n", (int)len);

    printf("HEX : ");
    for (size_t i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");

    printf("ASCII: ");
    for (size_t i = 0; i < len; i++) {
        char c = (char)buf[i];
        printf("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    printf("\n=================================\n\n");
}

static void cc1101_rx_task(void *arg)
{
    uint32_t io_num;
    uint8_t rxbuf[PKT_PAYLOAD_LEN + 2]; /* +2 for appended RSSI/LQI status bytes */

    for (;;) {
        if (xQueueReceive(gdo0_evt_queue, &io_num, portMAX_DELAY)) {
            /* Falling edge on GDO0 => packet fully received */
            uint8_t rxbytes = cc1101_read_status_reg(CC1101_RXBYTES) & 0x7F;

            if (rxbytes >= (PKT_PAYLOAD_LEN + 2)) {
                cc1101_read_burst(CC1101_RXFIFO, rxbuf, PKT_PAYLOAD_LEN + 2);

                int8_t rssi_raw = (int8_t)rxbuf[PKT_PAYLOAD_LEN];
                int rssi_dbm = (rssi_raw >= 128) ? ((rssi_raw - 256) / 2) - 74
                                                  : (rssi_raw / 2) - 74;
                uint8_t lqi = rxbuf[PKT_PAYLOAD_LEN + 1] & 0x7F;

                print_packet(rxbuf, PKT_PAYLOAD_LEN);
                printf("RSSI: %d dBm   LQI: %d\n\n", rssi_dbm, lqi);
            } else if (rxbytes > 0) {
                ESP_LOGW(TAG, "Unexpected RX byte count: %d (flushing)", rxbytes);
            }

            /* Flush and re-arm receiver defensively */
            cc1101_strobe(CC1101_SIDLE);
            cc1101_strobe(CC1101_SFRX);
            cc1101_strobe(CC1101_SRX);
        }
    }
}

/* ---------------- Init ---------------- */

static void spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000, /* 4 MHz, well within CC1101's limit */
        .mode = 0,
        .spics_io_num = -1, /* CSN handled manually (need to poll MISO after CS low) */
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_USED, &devcfg, &spi));

    gpio_config_t csn_conf = {
        .pin_bit_mask = (1ULL << PIN_CSN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&csn_conf);
    gpio_set_level(PIN_CSN, 1);
}

static void gdo0_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_GDO0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, /* falling edge = packet complete */
    };
    gpio_config(&io_conf);

    gdo0_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_GDO0, gdo0_isr_handler, (void *)PIN_GDO0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing SPI bus...");
    spi_init();

    ESP_LOGI(TAG, "Resetting CC1101...");
    cc1101_reset();

    uint8_t partnum = cc1101_read_status_reg(0x30 /* PARTNUM */);
    uint8_t version = cc1101_read_status_reg(0x31 /* VERSION */);
    ESP_LOGI(TAG, "CC1101 PARTNUM=0x%02X VERSION=0x%02X (VERSION should read 0x14 or 0x04 on genuine parts)",
             partnum, version);

    ESP_LOGI(TAG, "Configuring CC1101 for 433.92 MHz ASK/OOK...");
    cc1101_configure();

    gdo0_init();

    cc1101_strobe(CC1101_SIDLE);
    cc1101_strobe(CC1101_SFRX);
    cc1101_strobe(CC1101_SRX);

    ESP_LOGI(TAG, "CC1101 is now listening for OOK packets at 433.92 MHz.");

    xTaskCreate(cc1101_rx_task, "cc1101_rx_task", 4096, NULL, 10, NULL);
}
