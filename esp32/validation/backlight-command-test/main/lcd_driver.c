#include "lcd_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";
static spi_device_handle_t s_lcd_spi;

typedef struct {
    uint8_t command;
    uint8_t data[15];
    uint8_t data_length;
    uint16_t delay_ms;
} lcd_init_command_t;

// Vendor sequence from SX024-QVGA-45P-01_init(1).h for MD024-QVGA-01-V01.
static const lcd_init_command_t s_vendor_init[] = {
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
    // Landscape orientation matching the CAD: 11-pin header left, FPC right.
    // MX | MV | BGR compensates the module's counter-clockwise physical rotation.
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

static esp_err_t lcd_transmit(bool data_mode, const void *data, size_t length)
{
    if (length == 0) {
        return ESP_OK;
    }

    gpio_set_level(LCD_PIN_DC, data_mode ? 1 : 0);
    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd_spi, &transaction);
}

static esp_err_t lcd_command(uint8_t command, const uint8_t *data, size_t length)
{
    ESP_RETURN_ON_ERROR(lcd_transmit(false, &command, 1), TAG, "command failed");
    return lcd_transmit(true, data, length);
}

static esp_err_t lcd_backlight_gpio_init(void)
{
    // BLK was measured at about 0.2 mA when pulled to GND. Set the output
    // latch high before enabling open-drain mode so startup leaves BLK released.
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BLK, 1), TAG,
                        "BLK output preset failed");
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << LCD_PIN_BLK,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "BLK GPIO init failed");
    return gpio_set_level(LCD_PIN_BLK, 1);
}

static void lcd_hardware_reset(void)
{
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t lcd_set_window(int x0, int y0, int x1, int y1)
{
    const uint8_t columns[] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1,
    };
    const uint8_t rows[] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1,
    };
    ESP_RETURN_ON_ERROR(lcd_command(0x2A, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(lcd_command(0x2B, rows, sizeof(rows)), TAG, "row window");
    return lcd_command(0x2C, NULL, 0);
}

esp_err_t lcd_driver_init(void)
{
    ESP_RETURN_ON_ERROR(lcd_backlight_gpio_init(), TAG,
                        "backlight GPIO initialization failed");
    const gpio_config_t io_config = {
        .pin_bit_mask = (UINT64_C(1) << LCD_PIN_DC) | (UINT64_C(1) << LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "control GPIO init failed");

    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 3,
        .spics_io_num = LCD_PIN_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device_config, &s_lcd_spi),
                        TAG, "SPI device init failed");

    lcd_hardware_reset();
    for (size_t i = 0; i < sizeof(s_vendor_init) / sizeof(s_vendor_init[0]); ++i) {
        const lcd_init_command_t *entry = &s_vendor_init[i];
        ESP_RETURN_ON_ERROR(lcd_command(entry->command, entry->data, entry->data_length),
                            TAG, "vendor init failed at command 0x%02X", entry->command);
        if (entry->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(entry->delay_ms));
        }
    }

    ESP_LOGI(TAG, "ILI9341V ready: 320x240 landscape, SPI mode 3, 10 MHz");
    return ESP_OK;
}

esp_err_t lcd_driver_set_backlight(bool enabled)
{
    // Open-drain HIGH releases BLK (electrically floating); LOW connects BLK
    // to GND. Never drive BLK high with a push-pull output.
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BLK, enabled ? 1 : 0), TAG,
                        "BLK GPIO control failed");
    ESP_LOGI(TAG, "Physical backlight: %s (GPIO15 %s)",
             enabled ? "ON" : "OFF", enabled ? "RELEASED" : "LOW");
    return ESP_OK;
}

void lcd_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    if (x + width > LCD_WIDTH) width = LCD_WIDTH - x;
    if (y + height > LCD_HEIGHT) height = LCD_HEIGHT - y;
    if (lcd_set_window(x, y, x + width - 1, y + height - 1) != ESP_OK) return;

    uint8_t pixels[512];
    for (size_t i = 0; i < sizeof(pixels); i += 2) {
        pixels[i] = (uint8_t)(color >> 8);
        pixels[i + 1] = (uint8_t)color;
    }

    size_t remaining = (size_t)width * (size_t)height;
    gpio_set_level(LCD_PIN_DC, 1);
    while (remaining > 0) {
        size_t count = remaining > (sizeof(pixels) / 2) ? (sizeof(pixels) / 2) : remaining;
        spi_transaction_t transaction = {
            .length = count * 16,
            .tx_buffer = pixels,
        };
        if (spi_device_polling_transmit(s_lcd_spi, &transaction) != ESP_OK) return;
        remaining -= count;
    }
}

void lcd_fill_screen(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

static const uint8_t s_font[59][5] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00}, ['%' - 32] = {0x62,0x64,0x08,0x13,0x23},
    ['-' - 32] = {0x08,0x08,0x08,0x08,0x08},
    ['.' - 32] = {0x00,0x60,0x60,0x00,0x00}, ['0' - 32] = {0x3E,0x51,0x49,0x45,0x3E},
    ['1' - 32] = {0x00,0x42,0x7F,0x40,0x00}, ['2' - 32] = {0x42,0x61,0x51,0x49,0x46},
    ['3' - 32] = {0x21,0x41,0x45,0x4B,0x31}, ['4' - 32] = {0x18,0x14,0x12,0x7F,0x10},
    ['5' - 32] = {0x27,0x45,0x45,0x45,0x39}, ['6' - 32] = {0x3C,0x4A,0x49,0x49,0x30},
    ['7' - 32] = {0x01,0x71,0x09,0x05,0x03}, ['8' - 32] = {0x36,0x49,0x49,0x49,0x36},
    ['9' - 32] = {0x06,0x49,0x49,0x29,0x1E}, [':' - 32] = {0x00,0x36,0x36,0x00,0x00},
    ['A' - 32] = {0x7E,0x11,0x11,0x11,0x7E}, ['B' - 32] = {0x7F,0x49,0x49,0x49,0x36},
    ['C' - 32] = {0x3E,0x41,0x41,0x41,0x22}, ['D' - 32] = {0x7F,0x41,0x41,0x22,0x1C},
    ['E' - 32] = {0x7F,0x49,0x49,0x49,0x41}, ['F' - 32] = {0x7F,0x09,0x09,0x09,0x01},
    ['G' - 32] = {0x3E,0x41,0x49,0x49,0x7A}, ['H' - 32] = {0x7F,0x08,0x08,0x08,0x7F},
    ['I' - 32] = {0x00,0x41,0x7F,0x41,0x00}, ['K' - 32] = {0x7F,0x08,0x14,0x22,0x41},
    ['J' - 32] = {0x20,0x40,0x41,0x3F,0x01},
    ['L' - 32] = {0x7F,0x40,0x40,0x40,0x40}, ['M' - 32] = {0x7F,0x02,0x0C,0x02,0x7F},
    ['N' - 32] = {0x7F,0x04,0x08,0x10,0x7F}, ['O' - 32] = {0x3E,0x41,0x41,0x41,0x3E},
    ['P' - 32] = {0x7F,0x09,0x09,0x09,0x06}, ['R' - 32] = {0x7F,0x09,0x19,0x29,0x46},
    ['Q' - 32] = {0x3E,0x41,0x51,0x21,0x5E},
    ['S' - 32] = {0x46,0x49,0x49,0x49,0x31}, ['T' - 32] = {0x01,0x01,0x7F,0x01,0x01},
    ['U' - 32] = {0x3F,0x40,0x40,0x40,0x3F}, ['V' - 32] = {0x1F,0x20,0x40,0x20,0x1F},
    ['W' - 32] = {0x3F,0x40,0x38,0x40,0x3F}, ['X' - 32] = {0x63,0x14,0x08,0x14,0x63},
    ['Y' - 32] = {0x07,0x08,0x70,0x08,0x07},
    ['Z' - 32] = {0x61,0x51,0x49,0x45,0x43},
};

static void lcd_draw_char(int x, int y, char character, uint16_t foreground,
                          uint16_t background, int scale)
{
    if (character < 32 || character > 90) character = ' ';
    const uint8_t *glyph = s_font[(unsigned char)character - 32];
    for (int column = 0; column < 6; ++column) {
        uint8_t bits = column < 5 ? glyph[column] : 0;
        for (int row = 0; row < 8; ++row) {
            lcd_fill_rect(x + column * scale, y + row * scale, scale, scale,
                          (bits & (1U << row)) ? foreground : background);
        }
    }
}

void lcd_draw_text(int x, int y, const char *text, uint16_t foreground,
                   uint16_t background, int scale)
{
    if (text == NULL || scale < 1) return;
    while (*text != '\0') {
        lcd_draw_char(x, y, *text++, foreground, background, scale);
        x += 6 * scale;
    }
}
