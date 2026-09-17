#include "network_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "websocket_server.h"

#ifndef KVM_HAS_LOCAL_CONFIG
#define KVM_HAS_LOCAL_CONFIG 0
#endif

#if KVM_HAS_LOCAL_CONFIG
#include "kvm_config.local.h"
#else
#define KVM_WIFI_SSID ""
#define KVM_WIFI_PASSWORD ""
#endif

#ifndef KVM_WIFI_SSID
#define KVM_WIFI_SSID ""
#endif
#ifndef KVM_WIFI_PASSWORD
#define KVM_WIFI_PASSWORD ""
#endif

#define RECONNECT_MAX_DELAY_MS 30000

static const char *TAG = "network";
static app_state_t *s_state;
static esp_timer_handle_t s_reconnect_timer;
static uint32_t s_reconnect_delay_ms = 1000;

static void reconnect_timer_callback(void *argument)
{
    (void)argument;
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) ESP_LOGW(TAG, "Reconnect failed to start: %s",
                                   esp_err_to_name(result));
}

static void schedule_reconnect(void)
{
    if (s_reconnect_timer == NULL || esp_timer_is_active(s_reconnect_timer)) return;
    ESP_LOGI(TAG, "Wi-Fi reconnect in %" PRIu32 " ms", s_reconnect_delay_ms);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_reconnect_delay_ms * 1000);
    if (s_reconnect_delay_ms < RECONNECT_MAX_DELAY_MS) {
        s_reconnect_delay_ms *= 2;
        if (s_reconnect_delay_ms > RECONNECT_MAX_DELAY_MS) {
            s_reconnect_delay_ms = RECONNECT_MAX_DELAY_MS;
        }
    }
}

static void event_handler(void *argument, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)argument;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        const esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) ESP_LOGW(TAG, "Initial connect failed: %s",
                                       esp_err_to_name(result));
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        websocket_server_stop();
        app_state_set_wifi(s_state, false, NULL, "WIFI DISCONNECTED");
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u)", event != NULL ? event->reason : 0);
        schedule_reconnect();
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        char address[16];
        snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
        s_reconnect_delay_ms = 1000;
        if (s_reconnect_timer != NULL && esp_timer_is_active(s_reconnect_timer)) {
            esp_timer_stop(s_reconnect_timer);
        }
        app_state_set_wifi(s_state, true, address, "WIFI CONNECTED");
        ESP_LOGI(TAG, "Wi-Fi connected at %s", address);
        const esp_err_t result = websocket_server_start();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(result));
            app_state_set_last_message(s_state, "WEBSOCKET START FAILED");
        }
    }
}

esp_err_t network_manager_init(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    s_state = state;
    const bool configured = KVM_WIFI_SSID[0] != '\0';
    app_state_set_wifi_configured(state, configured);
    if (!configured) {
        app_state_set_wifi(state, false, NULL, "WIFI NOT CONFIGURED");
        ESP_LOGW(TAG, "Wi-Fi config not found at esp32/kvm_config.local.h");
        return ESP_OK;
    }
    if (strlen(KVM_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        strlen(KVM_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase");
        result = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(result, TAG, "NVS init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init");
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
    const esp_sntp_config_t time_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&time_config), TAG, "SNTP init");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG,
                        "disable Wi-Fi power save");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   event_handler, NULL), TAG,
                        "Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   event_handler, NULL), TAG,
                        "IP event handler");
    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_reconnect_timer), TAG,
                        "reconnect timer");
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, KVM_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, KVM_WIFI_PASSWORD, sizeof(config.sta.password));
    config.sta.threshold.authmode = KVM_WIFI_PASSWORD[0] == '\0'
        ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                        "station config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");
    ESP_LOGI(TAG, "Wi-Fi station started; credentials are never logged");
    return ESP_OK;
}
