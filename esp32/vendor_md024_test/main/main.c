#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* The existing sealed-box wiring. */
#define LCD_PIN_DC GPIO_NUM_9
#define LCD_PIN_CS GPIO_NUM_10
#define LCD_PIN_MOSI GPIO_NUM_11
#define LCD_PIN_SCLK GPIO_NUM_12
#define LCD_PIN_RST GPIO_NUM_14
#define LCD_PIN_BLK GPIO_NUM_15

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define BLK_PWM_FREQUENCY_HZ 20000
#define BLK_PWM_MAX_DUTY 1023

static const char *TAG = "md024_vendor_test";

typedef struct {
    uint32_t duty;
    const char *label;
} backlight_level_t;

static const backlight_level_t s_backlight_levels[] = {
    {310, "1.0 V equivalent / 30%"},
    {620, "2.0 V equivalent / 61%"},
    {1023, "3.3 V equivalent / 100%"},
};

typedef struct {
    uint8_t command;
    uint8_t data[15];
    uint8_t data_length;
    uint16_t delay_ms;
} init_command_t;

/* Exact MD024-QVGA-01-V01 / ILI9341V vendor sequence.
 * MADCTL is adjusted from portrait 0x08 to the verified landscape value 0x68. */
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
    {0x36, {0x68}, 1, 0},
    {0xB1, {0x00, 0x13}, 2, 0},
    {0xB6, {0x0A, 0xA2}, 2, 0},
    {0xF2, {0x02}, 1, 0},
    {0xE0, {0x0F, 0x27, 0x24, 0x0C, 0x10, 0x08, 0x55, 0x87,
            0x45, 0x08, 0x14, 0x07, 0x13, 0x08, 0x00}, 15, 0},
    {0xE1, {0x00, 0x0F, 0x12, 0x05, 0x11, 0x06, 0x25, 0x34,
            0x37, 0x01, 0x08, 0x07, 0x2B, 0x34, 0x0F}, 15, 0},
    {0x11, {0}, 0, 120},
    {0x29, {0}, 0, 120},
};

static inline void software_spi_write(uint8_t value)
{
    /* Same bit order and clock edges as the vendor ESP32 example: mode 3, MSB first. */
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_set_level(LCD_PIN_MOSI, (value & 0x80U) != 0);
        value <<= 1;
        gpio_set_level(LCD_PIN_SCLK, 0);
        gpio_set_level(LCD_PIN_SCLK, 1);
    }
}

static void write_command(uint8_t command)
{
    gpio_set_level(LCD_PIN_DC, 0);
    software_spi_write(command);
}

static void write_data8(uint8_t data)
{
    gpio_set_level(LCD_PIN_DC, 1);
    software_spi_write(data);
}

static void write_color(uint16_t rgb565)
{
    software_spi_write((uint8_t)(rgb565 >> 8));
    software_spi_write((uint8_t)rgb565);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    write_command(0x2A);
    write_data8((uint8_t)(x0 >> 8));
    write_data8((uint8_t)x0);
    write_data8((uint8_t)(x1 >> 8));
    write_data8((uint8_t)x1);

    write_command(0x2B);
    write_data8((uint8_t)(y0 >> 8));
    write_data8((uint8_t)y0);
    write_data8((uint8_t)(y1 >> 8));
    write_data8((uint8_t)y1);
    write_command(0x2C);
    gpio_set_level(LCD_PIN_DC, 1);
}

static void fill_rect(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height,
                      uint16_t color)
{
    set_window(x0, y0, x0 + width - 1, y0 + height - 1);
    const uint32_t pixels = (uint32_t)width * height;
    for (uint32_t i = 0; i < pixels; ++i) write_color(color);
}

static void draw_test_bands(void)
{
    static const uint16_t colors[] = {
        0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF, 0x0000,
    };
    const uint16_t band_width = LCD_WIDTH / 8;
    for (unsigned i = 0; i < 8; ++i) {
        fill_rect(i * band_width, 0, band_width, LCD_HEIGHT, colors[i]);
    }
}

static void init_gpio(void)
{
    const gpio_config_t lcd_outputs = {
        .pin_bit_mask = (UINT64_C(1) << LCD_PIN_DC) |
                        (UINT64_C(1) << LCD_PIN_CS) |
                        (UINT64_C(1) << LCD_PIN_MOSI) |
                        (UINT64_C(1) << LCD_PIN_SCLK) |
                        (UINT64_C(1) << LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&lcd_outputs));

    gpio_set_level(LCD_PIN_CS, 0); /* Dedicated display: keep selected, like vendor test. */
    gpio_set_level(LCD_PIN_DC, 0);
    gpio_set_level(LCD_PIN_MOSI, 0);
    gpio_set_level(LCD_PIN_SCLK, 1);
}

static void init_backlight_pwm(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BLK_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = s_backlight_levels[0].duty,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    ESP_ERROR_CHECK(gpio_set_drive_capability(LCD_PIN_BLK, GPIO_DRIVE_CAP_0));
}

static void set_backlight_level(const backlight_level_t *level)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level->duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ESP_LOGI(TAG, "BLK PWM: %s", level->label);
}

static void reset_and_init_panel(void)
{
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    for (unsigned i = 0; i < sizeof(s_vendor_init) / sizeof(s_vendor_init[0]); ++i) {
        write_command(s_vendor_init[i].command);
        for (unsigned j = 0; j < s_vendor_init[i].data_length; ++j) {
            write_data8(s_vendor_init[i].data[j]);
        }
        if (s_vendor_init[i].delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(s_vendor_init[i].delay_ms));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "MD024 vendor-style isolated display test starting");
    init_gpio();
    init_backlight_pwm();
    reset_and_init_panel();

    ESP_LOGI(TAG, "Drawing static full-screen color bands");
    draw_test_bands();
    ESP_LOGI(TAG, "Static image; cycling PWM-equivalent BLK levels once per second");
    unsigned level_index = 0;
    while (true) {
        set_backlight_level(&s_backlight_levels[level_index]);
        level_index = (level_index + 1) %
                      (sizeof(s_backlight_levels) / sizeof(s_backlight_levels[0]));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
