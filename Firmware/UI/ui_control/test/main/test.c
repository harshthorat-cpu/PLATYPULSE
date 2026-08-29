#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lvgl.h"

#include "ir_ctrl.h"
#include "pn532_ctrl.h"

/* ============================================================
 *  DISPLAY WIRING  (ILI9341, 128x160 module)
 * ============================================================ */
#define TFT_HOST       SPI2_HOST
#define TFT_MOSI       23
#define TFT_SCLK       18
#define TFT_CS         5
#define TFT_DC         2
#define TFT_RST        4
#define TFT_BL         21
#define TFT_MISO       19   /* shared SPI2 bus: must be a real pin so PN532 reads work (see spi.c) */

#define TFT_H_RES      128
#define TFT_V_RES      160

/* ============================================================
 *  JOYSTICK WIRING (HW-504)
 *  NOTE: JOY_SW_PIN must not collide with the RFID IRQ pin (25)
 *  or the IR RX pin (25 in store_and_emulate.c) used elsewhere
 *  in this project -- rewire the joystick button to a free GPIO
 *  (33 is used below) if you are also using pn532_ctrl / ir_ctrl.
 * ============================================================ */
#define JOY_X_CHAN   ADC1_CHANNEL_6   // GPIO 34
#define JOY_Y_CHAN   ADC1_CHANNEL_7   // GPIO 35
#define JOY_SW_PIN   GPIO_NUM_33

static const char *TAG = "main";
static esp_lcd_panel_handle_t panel_handle = NULL;

LV_IMAGE_DECLARE(Perry_the_platypus);

/* ------------------------------------------------------------
 * Joystick calibration + edge-detected "encoder" state
 * ------------------------------------------------------------ */
static int centerX = 0, centerY = 0;
static bool joy_axis_armed = true;   // true = free to fire a new step

/* Which "memory slot" each menu button maps to */
typedef enum {
    SLOT_IR = 0,
    SLOT_NFC,
    SLOT_RF,
    SLOT_COUNT
} slot_id_t;

static const char *slot_names[SLOT_COUNT] = { "IR", "NFC/RFID", "RF" };

/* ============================================================
 *  LVGL FLUSH CALLBACK
 * ============================================================ */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

/* ============================================================
 *  JOYSTICK -> LVGL ENCODER INDEV
 * ============================================================ */
static void joystick_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int xRaw = adc1_get_raw(JOY_X_CHAN);
    int yRaw = adc1_get_raw(JOY_Y_CHAN);
    int btnRaw = gpio_get_level(JOY_SW_PIN); // 0 = pressed

    int xVal = (xRaw - centerX) / 20;
    int yVal = (centerY - yRaw) / 20;

    if (xVal > 100) xVal = 100;
    if (xVal < -100) xVal = -100;
    if (yVal > 100) yVal = 100;
    if (yVal < -100) yVal = -100;

    data->enc_diff = 0;

    const int THRESH = 40;
    const int RELEASE = 15;

    if (joy_axis_armed) {
        if (yVal < -THRESH) {
            data->enc_diff = -1;
            joy_axis_armed = false;
        } else if (yVal > THRESH) {
            data->enc_diff = 1;
            joy_axis_armed = false;
        }
    } else {
        if (yVal > -RELEASE && yVal < RELEASE) {
            joy_axis_armed = true;
        }
    }

    data->state = (btnRaw == 0) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    (void)xVal;
}

static void joystick_calibrate(void)
{
    ESP_LOGI(TAG, "Calibrating joystick, keep it centered...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    long sx = 0, sy = 0;
    for (int i = 0; i < 16; i++) {
        sx += adc1_get_raw(JOY_X_CHAN);
        sy += adc1_get_raw(JOY_Y_CHAN);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    centerX = sx / 16;
    centerY = sy / 16;
    ESP_LOGI(TAG, "Joystick center -> X:%d Y:%d", centerX, centerY);
}

/* ============================================================
 *  Shared indev / group handles
 * ============================================================ */
static lv_indev_t *g_joy_indev = NULL;
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *menu_screen = NULL;

static void go_to_menu(void);

/* ============================================================
 *  Small helper: builds the common "title + status label +
 *  Start/Back button" layout shared by all three sub-screens.
 * ============================================================ */
typedef struct {
    lv_obj_t *scr;
    lv_obj_t *status_label;
    lv_obj_t *btn_start;
    lv_obj_t *btn_back;
} sub_screen_widgets_t;

static sub_screen_widgets_t build_sub_screen(const char *title_text,
                                              lv_event_cb_t start_cb,
                                              lv_event_cb_t back_cb)
{
    sub_screen_widgets_t w = {0};

    w.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(w.scr, lv_color_black(), 0);
    lv_obj_set_flex_flow(w.scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(w.scr, 6, 0);

    lv_obj_t *title = lv_label_create(w.scr);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    w.status_label = lv_label_create(w.scr);
    lv_label_set_text(w.status_label, "Idle");
    lv_label_set_long_mode(w.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(w.status_label, 110);
    lv_obj_set_style_text_align(w.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(w.status_label, lv_color_white(), 0);

    w.btn_start = lv_button_create(w.scr);
    lv_obj_set_size(w.btn_start, 100, 32);
    lv_obj_add_event_cb(w.btn_start, start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_start = lv_label_create(w.btn_start);
    lv_label_set_text(lbl_start, "Start");
    lv_obj_center(lbl_start);

    w.btn_back = lv_button_create(w.scr);
    lv_obj_set_size(w.btn_back, 100, 32);
    lv_obj_add_event_cb(w.btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(w.btn_back);
    lv_label_set_text(lbl_back, "Back");
    lv_obj_center(lbl_back);

    lv_group_t *g = lv_group_create();
    lv_indev_set_group(g_joy_indev, g);
    lv_group_add_obj(g, w.btn_start);
    lv_group_add_obj(g, w.btn_back);
    lv_group_focus_obj(w.btn_start);

    return w;
}

/* ============================================================
 *  IR SUB-SCREEN
 *  Flow: Start -> "Please store the signal" (record mode) ->
 *        once a signal is captured -> "Emulating..." (loops
 *        playback). Back stops the task and returns to the menu.
 * ============================================================ */
static lv_obj_t *ir_screen = NULL;
static lv_obj_t *ir_status_label = NULL;
static lv_obj_t *ir_btn_start = NULL;
static lv_timer_t *ir_poll_timer = NULL;

static void ir_start_cb(lv_event_t *e);
static void ir_back_cb(lv_event_t *e);

static void ir_poll_timer_cb(lv_timer_t *t)
{
    ir_state_t st = ir_ctrl_get_state();
    const char *txt;
    switch (st) {
        case IR_STATE_RECORDING:
            txt = "Please store the signal\nPoint remote at sensor\nand press a button";
            break;
        case IR_STATE_EMITTING:
            txt = "Signal captured!\nEmulating...";
            break;
        default:
            txt = "Idle";
            break;
    }
    if (ir_status_label) {
        lv_label_set_text(ir_status_label, txt);
    }
}

static lv_obj_t *make_ir_screen(void)
{
    sub_screen_widgets_t w = build_sub_screen("IR", ir_start_cb, ir_back_cb);
    ir_status_label = w.status_label;
    ir_btn_start = w.btn_start;
    ir_screen = w.scr;
    return w.scr;
}

static void ir_start_cb(lv_event_t *e)
{
    ir_ctrl_start();
    if (ir_btn_start) {
        lv_obj_add_state(ir_btn_start, LV_STATE_DISABLED);
    }
    if (ir_status_label) {
        lv_label_set_text(ir_status_label, "Please store the signal\nPoint remote at sensor\nand press a button");
    }
    if (ir_poll_timer == NULL) {
        ir_poll_timer = lv_timer_create(ir_poll_timer_cb, 300, NULL);
    }
}

static void ir_back_cb(lv_event_t *e)
{
    ir_ctrl_stop();
    if (ir_poll_timer) {
        lv_timer_delete(ir_poll_timer);
        ir_poll_timer = NULL;
    }
    ir_status_label = NULL;
    ir_btn_start = NULL;
    go_to_menu();
    ir_screen = NULL;
}

/* ============================================================
 *  NFC/RFID SUB-SCREEN
 *  Flow: Start -> "Scanning..." -> once a card is detected the
 *        UID is printed on screen, then scanning resumes for
 *        the next card. Back stops the task and returns to menu.
 * ============================================================ */
static lv_obj_t *nfc_screen = NULL;
static lv_obj_t *nfc_status_label = NULL;
static lv_obj_t *nfc_btn_start = NULL;
static lv_timer_t *nfc_poll_timer = NULL;

static void nfc_start_cb(lv_event_t *e);
static void nfc_back_cb(lv_event_t *e);

static void nfc_poll_timer_cb(lv_timer_t *t)
{
    nfc_state_t st = pn532_ctrl_get_state();
    char buf[64];

    if (st == NFC_STATE_GOT_UID) {
        char uid[16];
        pn532_ctrl_get_uid_str(uid, sizeof(uid));
        snprintf(buf, sizeof(buf), "Card detected!\nUID:\n%s", uid);
    } else if (st == NFC_STATE_WAITING) {
        snprintf(buf, sizeof(buf), "Scanning...\nPresent a card");
    } else {
        snprintf(buf, sizeof(buf), "Idle");
    }

    if (nfc_status_label) {
        lv_label_set_text(nfc_status_label, buf);
    }
}

static lv_obj_t *make_nfc_screen(void)
{
    sub_screen_widgets_t w = build_sub_screen("NFC / RFID", nfc_start_cb, nfc_back_cb);
    nfc_status_label = w.status_label;
    nfc_btn_start = w.btn_start;
    nfc_screen = w.scr;
    return w.scr;
}

static void nfc_start_cb(lv_event_t *e)
{
    pn532_ctrl_start();
    if (nfc_btn_start) {
        lv_obj_add_state(nfc_btn_start, LV_STATE_DISABLED);
    }
    if (nfc_status_label) {
        lv_label_set_text(nfc_status_label, "Scanning...\nPresent a card");
    }
    if (nfc_poll_timer == NULL) {
        nfc_poll_timer = lv_timer_create(nfc_poll_timer_cb, 300, NULL);
    }
}

static void nfc_back_cb(lv_event_t *e)
{
    pn532_ctrl_stop();
    if (nfc_poll_timer) {
        lv_timer_delete(nfc_poll_timer);
        nfc_poll_timer = NULL;
    }
    nfc_status_label = NULL;
    nfc_btn_start = NULL;
    go_to_menu();
    nfc_screen = NULL;
}

/* ============================================================
 *  RF SUB-SCREEN (placeholder -- no RF driver wired up yet)
 *  Kept with the same Start/Back layout as IR and NFC so the UI
 *  is consistent; Start just reports that RF isn't wired up.
 * ============================================================ */
static lv_obj_t *rf_screen = NULL;
static lv_obj_t *rf_status_label = NULL;

static void rf_start_cb(lv_event_t *e);
static void rf_back_cb(lv_event_t *e);

static lv_obj_t *make_rf_screen(void)
{
    sub_screen_widgets_t w = build_sub_screen("RF", rf_start_cb, rf_back_cb);
    rf_status_label = w.status_label;
    lv_label_set_text(w.status_label, "Not implemented yet");
    rf_screen = w.scr;
    return w.scr;
}

static void rf_start_cb(lv_event_t *e)
{
    if (rf_status_label) {
        lv_label_set_text(rf_status_label, "RF driver not\nwired up yet");
    }
}

static void rf_back_cb(lv_event_t *e)
{
    rf_status_label = NULL;
    go_to_menu();
    rf_screen = NULL;
}

/* ============================================================
 *  MAIN MENU (3 stacked buttons)
 * ============================================================ */
static void menu_btn_event_cb(lv_event_t *e)
{
    slot_id_t slot = (slot_id_t)(intptr_t)lv_event_get_user_data(e);

    lv_obj_t *next = NULL;
    switch (slot) {
        case SLOT_IR:  next = make_ir_screen();  break;
        case SLOT_NFC: next = make_nfc_screen(); break;
        case SLOT_RF:  next = make_rf_screen();  break;
        default: return;
    }

    lv_obj_t *old_menu = menu_screen;
    lv_screen_load(next);
    if (old_menu != NULL) {
        lv_obj_delete(old_menu);
        menu_screen = NULL;
    }
}

static lv_obj_t *make_menu_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 6, 0);

    lv_group_t *g = lv_group_create();
    lv_indev_set_group(g_joy_indev, g);

    for (int i = 0; i < SLOT_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_size(btn, 100, 32);
        lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, slot_names[i]);
        lv_obj_center(label);

        lv_group_add_obj(g, btn);
    }

    lv_group_focus_obj(lv_obj_get_child(scr, 0));
    return scr;
}

static void go_to_menu(void)
{
    lv_obj_t *old = lv_screen_active();
    menu_screen = make_menu_screen();
    lv_screen_load(menu_screen);
    if (old != NULL) {
        lv_obj_delete(old);
    }
}

/* ============================================================
 *  Splash screen (Perry image, shown for 5 seconds)
 * ============================================================ */
static void splash_timer_cb(lv_timer_t *timer)
{
    go_to_menu();
    lv_timer_delete(timer);
}

static void show_splash_screen(void)
{
    splash_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash_screen, lv_color_black(), 0);

    lv_obj_t *img = lv_image_create(splash_screen);
    lv_image_set_src(img, &Perry_the_platypus);
    lv_obj_center(img);

    lv_screen_load(splash_screen);
    lv_timer_create(splash_timer_cb, 5000, NULL);
}

/* ============================================================
 *  Display / hardware init
 * ============================================================ */
static void display_hw_init(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << TFT_BL
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(TFT_BL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = TFT_SCLK,
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = TFT_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TFT_DC,
        .cs_gpio_num = TFT_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    /* ILI9341's controller RAM is natively 240x320; on a 128x160 module the
     * visible glass is offset within that RAM. If the image appears
     * shifted/cropped, adjust these gap values (common values to try:
     * 0,0 / 0,32 / 32,0 -- check your module's datasheet). */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    /* Screens in this app use black backgrounds; true is a common
     * requirement for ILI9341 modules -- flip this if colors look
     * inverted on your specific module. */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

static void joystick_hw_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(JOY_X_CHAN, ADC_ATTEN_DB_12);
    adc1_config_channel_atten(JOY_Y_CHAN, ADC_ATTEN_DB_12);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << JOY_SW_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    joystick_calibrate();
}

/* ============================================================
 *  app_main
 * ============================================================ */
void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    display_hw_init();
    joystick_hw_init();

    /* Bring up the peripheral drivers so the sub-screens can just
     * call start()/stop() without worrying about init order. If a
     * driver fails to init (e.g. hardware not wired yet), log it and
     * let that sub-screen simply report "Idle" instead of crashing. */
    if (ir_ctrl_init() != ESP_OK) {
        ESP_LOGE(TAG, "ir_ctrl_init failed");
    }
    if (pn532_ctrl_init() != ESP_OK) {
        ESP_LOGE(TAG, "pn532_ctrl_init failed");
    }

    lv_init();

    lv_display_t *disp = lv_display_create(TFT_H_RES, TFT_V_RES);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    uint32_t buf_px = TFT_H_RES * 40;
    uint16_t *buf1 = malloc(buf_px * sizeof(uint16_t));
    uint16_t *buf2 = malloc(buf_px * sizeof(uint16_t));
    assert(buf1 != NULL && buf2 != NULL);
    lv_display_set_buffers(disp, buf1, buf2, buf_px * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    g_joy_indev = lv_indev_create();
    lv_indev_set_type(g_joy_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(g_joy_indev, joystick_indev_read_cb);

    show_splash_screen();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_tick_inc(10);
        lv_timer_handler();
    }
}