#include "lcd_lvgl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint8_t command;
    uint8_t data[15];
    uint8_t data_length;
    uint16_t delay_ms;
} init_command_t;

static const char *TAG = "lcd_lvgl";
static esp_lcd_panel_io_handle_t s_panel_io;
static lv_display_t *s_display;
static void *s_draw_buffer;

/* Verified vendor sequence for MD024-QVGA-01-V01 / ILI9341V. */
static const init_command_t s_vendor_init[] = {
    {0x3A, {0x55}, 1, 0},
    {0xF6, {0x01, 0x33}, 2, 0},
    {0xB5, {0x04, 0x04, 0x0A, 0x14}, 4, 0},
    {0x35, {0x00}, 1, 0},
    {0xCF, {0x00, 0xEA, 0xF0}, 3, 0},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, {0x85, 0x00, 0x78}, 3, 0},
    {0xCB, {0x39, 0x2C, 0x00, 0x33, 0x06}, 5, 0},
    {0xF7, {0x20}, 1, 0},
    {0xEA, {0x00, 0x00}, 2, 0},
    {0xC0, {0x21}, 1, 0},
    {0xC1, {0x10}, 1, 0},
    {0xC5, {0x4F, 0x38}, 2, 0},
    {0xC7, {0x98}, 1, 0},
    /* Landscape: 11-pin header left, FPC right. MX | MV | BGR. */
    {0x36, {0x68}, 1, 0},
    {0xB1, {0x00, 0x13}, 2, 0},
    {0xB6, {0x0A, 0xA2}, 2, 0},
    {0xF2, {0x02}, 1, 0},
    {0xE0, {0x0F, 0x27, 0x24, 0x0C, 0x10, 0x08, 0x55, 0x87,
            0x45, 0x08, 0x14, 0x07, 0x13, 0x08, 0x00}, 15, 0},
    {0xE1, {0x00, 0x0F, 0x12, 0x05, 0x11, 0x06, 0x25, 0x34,
            0x37, 0x01, 0x08, 0x07, 0x2B, 0x34, 0x0F}, 15, 0},
    {0x11, {0}, 0, 120},
    {0x29, {0}, 0, 20},
};

static esp_err_t command(uint8_t value, const uint8_t *data, size_t length)
{
    return esp_lcd_panel_io_tx_param(s_panel_io, value, data, length);
}

static void hardware_reset(void)
{
    gpio_set_level(APP_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(APP_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_level(APP_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t set_window(int x0, int y0, int x1, int y1)
{
    const uint8_t columns[] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1,
    };
    const uint8_t rows[] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1,
    };
    ESP_RETURN_ON_ERROR(command(0x2A, columns, sizeof(columns)), TAG, "column window");
    return command(0x2B, rows, sizeof(rows));
}

static bool color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *event_data,
                                void *user_ctx)
{
    (void)panel_io;
    (void)event_data;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    const size_t pixel_count = (size_t)width * (size_t)height;
    esp_err_t result = set_window(area->x1, area->y1, area->x2, area->y2);
    if (result == ESP_OK) {
        /* ILI9341 expects RGB565 most-significant byte first. */
        for (size_t i = 0; i < pixel_count; ++i) {
            const uint8_t low = pixels[i * 2];
            pixels[i * 2] = pixels[i * 2 + 1];
            pixels[i * 2 + 1] = low;
        }
        result = esp_lcd_panel_io_tx_color(s_panel_io, 0x2C, pixels, pixel_count * 2);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(result));
        lv_display_flush_ready(display);
    }
}

esp_err_t lcd_lvgl_init(lv_display_t **display_out)
{
    if (display_out == NULL) return ESP_ERR_INVALID_ARG;
    const gpio_config_t controls = {
        .pin_bit_mask = (UINT64_C(1) << APP_LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&controls), TAG, "LCD control GPIO init");
    const spi_bus_config_t bus = {
        .mosi_io_num = APP_LCD_PIN_MOSI,
        .miso_io_num = APP_LCD_PIN_MISO,
        .sclk_io_num = APP_LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = APP_LCD_WIDTH * APP_LCD_DRAW_BUFFER_LINES * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG,
                        "SPI bus init");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = APP_LCD_PIN_CS,
        .dc_gpio_num = APP_LCD_PIN_DC,
        .spi_mode = 3,
        .pclk_hz = APP_LCD_SPI_CLOCK_HZ,
        .trans_queue_depth = 2,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config,
                                 &s_panel_io),
        TAG, "official esp_lcd SPI IO init");
    const gpio_num_t spi_outputs[] = {
        APP_LCD_PIN_DC,
        APP_LCD_PIN_CS,
        APP_LCD_PIN_MOSI,
        APP_LCD_PIN_SCLK,
    };
    for (size_t i = 0; i < sizeof(spi_outputs) / sizeof(spi_outputs[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_set_drive_capability(spi_outputs[i], GPIO_DRIVE_CAP_1),
                            TAG, "lower GPIO%d drive strength", spi_outputs[i]);
    }
    hardware_reset();
    for (size_t i = 0; i < sizeof(s_vendor_init) / sizeof(s_vendor_init[0]); ++i) {
        const init_command_t *entry = &s_vendor_init[i];
        ESP_RETURN_ON_ERROR(command(entry->command, entry->data, entry->data_length), TAG,
                            "vendor init 0x%02x", entry->command);
        if (entry->delay_ms > 0) vTaskDelay(pdMS_TO_TICKS(entry->delay_ms));
    }

    const size_t buffer_bytes = APP_LCD_WIDTH * APP_LCD_DRAW_BUFFER_LINES * 2;
    s_draw_buffer = heap_caps_malloc(buffer_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_draw_buffer == NULL) return ESP_ERR_NO_MEM;
    s_display = lv_display_create(APP_LCD_WIDTH, APP_LCD_HEIGHT);
    if (s_display == NULL) return ESP_ERR_NO_MEM;
    lv_display_set_flush_cb(s_display, flush);
    lv_display_set_buffers(s_display, s_draw_buffer, NULL, buffer_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = color_transfer_done,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_register_event_callbacks(s_panel_io, &callbacks, s_display),
        TAG, "LCD transfer callback init");
    *display_out = s_display;
    ESP_LOGI(TAG,
             "ILI9341V + LVGL ready via esp_lcd: 320x240, SPI mode 3, %u Hz, "
             "%u-byte DMA buffer",
             (unsigned)APP_LCD_SPI_CLOCK_HZ, (unsigned)buffer_bytes);
    return ESP_OK;
}
