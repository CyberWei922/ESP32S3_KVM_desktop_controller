#include "lcd_test_ui.h"

#include <stdio.h>
#include <string.h>

#include "lcd_driver.h"

static const uint16_t COLOR_BLACK = LCD_RGB565(0, 0, 0);
static const uint16_t COLOR_WHITE = LCD_RGB565(255, 255, 255);
static const uint16_t COLOR_GRAY = LCD_RGB565(45, 52, 64);
static const uint16_t COLOR_DIM = LCD_RGB565(148, 163, 184);
static const uint16_t COLOR_GREEN = LCD_RGB565(52, 211, 153);
static const uint16_t COLOR_YELLOW = LCD_RGB565(250, 204, 21);
static const uint16_t COLOR_RED = LCD_RGB565(248, 113, 113);
static const uint16_t COLOR_CYAN = LCD_RGB565(34, 211, 238);

static void draw_progress_bar(int x, int y, int width, unsigned int percent, uint16_t color)
{
    const int height = 12;
    const int inner_width = width - 4;
    if (percent > 100) percent = 100;
    lcd_fill_rect(x, y, width, height, COLOR_GRAY);
    lcd_fill_rect(x + 2, y + 2, inner_width, height - 4, COLOR_BLACK);
    lcd_fill_rect(x + 2, y + 2, (inner_width * (int)percent) / 100, height - 4, color);
}

static const char *status_text(const host_status_t *host)
{
    if (!host->online) return "OFFLINE";
    if (host->stale) return "STALE";
    return "ONLINE";
}

static uint16_t status_color(const host_status_t *host)
{
    if (!host->online) return COLOR_RED;
    if (host->stale) return COLOR_YELLOW;
    return COLOR_GREEN;
}

static void draw_changed_text(int x, int y, const char *text, uint16_t foreground,
                              int scale, char *cache, size_t cache_capacity)
{
    if (strcmp(text, cache) == 0) return;
    lcd_draw_text(x, y, text, foreground, COLOR_BLACK, scale);
    snprintf(cache, cache_capacity, "%s", text);
}

void lcd_test_ui_show(void)
{
    lcd_fill_screen(COLOR_BLACK);
    lcd_fill_rect(0, 0, 80, 4, LCD_RGB565(255, 0, 0));
    lcd_fill_rect(80, 0, 80, 4, LCD_RGB565(0, 255, 0));
    lcd_fill_rect(160, 0, 80, 4, LCD_RGB565(0, 0, 255));
    lcd_fill_rect(240, 0, 80, 4, COLOR_WHITE);

    lcd_draw_text(12, 13, "KVM CONTROL", COLOR_WHITE, COLOR_BLACK, 2);
    lcd_draw_text(224, 13, "LIVE 00000", COLOR_GREEN, COLOR_BLACK, 1);
    lcd_fill_rect(10, 36, 300, 2, COLOR_GRAY);

    lcd_draw_text(14, 46, "MAC OFFLINE  ", COLOR_RED, COLOR_BLACK, 2);
    lcd_draw_text(14, 72, "CPU: N/A    ", COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 92, 180, 0, COLOR_GREEN);
    lcd_draw_text(14, 116, "MEM: N/A    ", COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 136, 180, 0, COLOR_CYAN);

    lcd_fill_rect(207, 44, 2, 128, COLOR_GRAY);
    lcd_draw_text(217, 48, "WINDOWS", COLOR_CYAN, COLOR_BLACK, 1);
    lcd_draw_text(217, 63, "OFFLINE       ", COLOR_RED, COLOR_BLACK, 1);
    lcd_draw_text(217, 83, "CPU: N/A      ", COLOR_WHITE, COLOR_BLACK, 1);
    lcd_draw_text(217, 98, "MEM: N/A      ", COLOR_WHITE, COLOR_BLACK, 1);
    lcd_draw_text(217, 124, "PROTOCOL V1", COLOR_DIM, COLOR_BLACK, 1);
    lcd_draw_text(217, 139, "PORT 81", COLOR_DIM, COLOR_BLACK, 1);

    lcd_fill_rect(10, 178, 300, 2, COLOR_GRAY);
    lcd_draw_text(14, 188, "WIFI: NOT CONFIGURED              ", COLOR_YELLOW,
                  COLOR_BLACK, 1);
    lcd_draw_text(14, 204, "IP: 0.0.0.0                      ", COLOR_DIM,
                  COLOR_BLACK, 1);
    lcd_draw_text(14, 220, "WAITING FOR STATSFORKVM           ", COLOR_DIM,
                  COLOR_BLACK, 1);
    lcd_fill_rect(0, 236, 320, 4, COLOR_WHITE);
}

void lcd_test_ui_update(const app_state_snapshot_t *snapshot, uint32_t frame_number)
{
    if (snapshot == NULL) return;
    const host_status_t *mac = &snapshot->hosts[HOST_MAC];
    const host_status_t *windows = &snapshot->hosts[HOST_WINDOWS];
    char text[48];
    static char mac_status_cache[16];
    static char mac_cpu_cache[16];
    static char mac_memory_cache[16];
    static char windows_status_cache[16];
    static char windows_cpu_cache[18];
    static char windows_memory_cache[18];
    static char wifi_cache[40];
    static char ip_cache[40];
    static char message_cache[48];
    static int previous_temperature_bar = -1;
    static int previous_memory_bar = -1;

    snprintf(text, sizeof(text), "LIVE %05lu", (unsigned long)(frame_number % 100000));
    lcd_draw_text(224, 13, text, COLOR_GREEN, COLOR_BLACK, 1);

    snprintf(text, sizeof(text), "MAC %-7s", status_text(mac));
    draw_changed_text(14, 46, text, status_color(mac), 2, mac_status_cache,
                      sizeof(mac_status_cache));
    if (mac->temperature_valid) {
        snprintf(text, sizeof(text), "CPU: %5.1fC ", (double)mac->cpu_temperature_c);
    } else {
        snprintf(text, sizeof(text), "CPU: N/A    ");
    }
    draw_changed_text(14, 72, text, COLOR_WHITE, 2, mac_cpu_cache, sizeof(mac_cpu_cache));
    unsigned int temperature_bar = mac->temperature_valid
        ? (unsigned int)(mac->cpu_temperature_c * 100.0f / 110.0f) : 0;
    if ((int)temperature_bar != previous_temperature_bar) {
        draw_progress_bar(14, 92, 180, temperature_bar, COLOR_GREEN);
        previous_temperature_bar = (int)temperature_bar;
    }

    if (mac->memory_valid) {
        snprintf(text, sizeof(text), "MEM: %5.1f%% ", (double)mac->memory_percent);
    } else {
        snprintf(text, sizeof(text), "MEM: N/A    ");
    }
    draw_changed_text(14, 116, text, COLOR_WHITE, 2, mac_memory_cache,
                      sizeof(mac_memory_cache));
    int memory_bar = mac->memory_valid ? (int)mac->memory_percent : 0;
    if (memory_bar != previous_memory_bar) {
        draw_progress_bar(14, 136, 180, (unsigned int)memory_bar, COLOR_CYAN);
        previous_memory_bar = memory_bar;
    }

    snprintf(text, sizeof(text), "%-13s", status_text(windows));
    draw_changed_text(217, 63, text, status_color(windows), 1, windows_status_cache,
                      sizeof(windows_status_cache));
    if (windows->temperature_valid) {
        snprintf(text, sizeof(text), "CPU: %5.1fC   ", (double)windows->cpu_temperature_c);
    } else {
        snprintf(text, sizeof(text), "CPU: N/A      ");
    }
    draw_changed_text(217, 83, text, COLOR_WHITE, 1, windows_cpu_cache,
                      sizeof(windows_cpu_cache));
    if (windows->memory_valid) {
        snprintf(text, sizeof(text), "MEM: %5.1f%%   ", (double)windows->memory_percent);
    } else {
        snprintf(text, sizeof(text), "MEM: N/A      ");
    }
    draw_changed_text(217, 98, text, COLOR_WHITE, 1, windows_memory_cache,
                      sizeof(windows_memory_cache));

    if (!snapshot->wifi_configured) {
        snprintf(text, sizeof(text), "WIFI: NOT CONFIGURED              ");
    } else if (snapshot->wifi_connected) {
        snprintf(text, sizeof(text), "WIFI: CONNECTED                   ");
    } else {
        snprintf(text, sizeof(text), "WIFI: CONNECTING                  ");
    }
    draw_changed_text(14, 188, text,
                      snapshot->wifi_connected ? COLOR_GREEN : COLOR_YELLOW, 1,
                      wifi_cache, sizeof(wifi_cache));
    snprintf(text, sizeof(text), "IP: %-15s                 ", snapshot->ip_address);
    draw_changed_text(14, 204, text, COLOR_DIM, 1, ip_cache, sizeof(ip_cache));
    snprintf(text, sizeof(text), "%-47.47s", snapshot->last_message);
    draw_changed_text(14, 220, text, COLOR_DIM, 1, message_cache, sizeof(message_cache));
}
