/* Captive Portal Example
    This example code is in the Public Domain (or CC0 licensed, at your option.)
    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_sleep.h"
// #include "esp32/ulp.h"
#include "driver/rtc_io.h"
// #include "ulp_main.h"
#include "string.h"
#include <sys/socket.h>
#include "driver/gpio.h"
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "esp_vfs.h"

#include "esp_http_server.h"
#include "dns_server.h"

#include "button.h"

#include "esp_sntp.h"

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#define DEFAULT_SCAN_LIST_SIZE CONFIG_EXAMPLE_SCAN_LIST_SIZE

#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY_CONNECT

#define BUTTON_PIN_BITMASK 0x400000000 // 2^34 in hex

#define GPIO_OUTPUT_STATUS_LED 32
// #define GPIO_OUTPUT_MAIN_LED 33
#define GPIO_OUTPUT_GND_MAIN 27
// #define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_OUTPUT_STATUS_LED) | (1ULL << GPIO_OUTPUT_MAIN_LED) | (1ULL << GPIO_OUTPUT_GND_MAIN))
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_OUTPUT_STATUS_LED) | (1ULL << GPIO_OUTPUT_GND_MAIN))

#define GPIO_INPUT_BTN 34
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_INPUT_BTN)

#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

extern const char script_start[] asm("_binary_script_js_start");
extern const char script_end[] asm("_binary_script_js_end");

extern const char style_start[] asm("_binary_style_css_start");
extern const char style_end[] asm("_binary_style_css_end");

//extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
//extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static const char *TAG = "ESP32_RTC_LIGHT";

static QueueHandle_t go_sleep_evt_queue = NULL;

nvs_handle_t nvs_handler;
size_t length = 0;

// Time Settings
static char *onHr = "";
static char *onMn = "";
static char *offHr = "";
static char *offMn = "";
// Time Server Settings
static char *timeServer = "";
// AP Settings
static char *ssid = "";
static char *passwd = "";
// OTA
static char *otaServer = "";

static void wifi_scan();
static void simple_ota_example_task();
static void go_into_deep_sleep();
static void restore_settings();
static void set_led_task();
void set_led();

static void IRAM_ATTR go_sleep_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(go_sleep_evt_queue, &gpio_num, NULL);
}

static void go_sleep_task(void *arg)
{
    uint32_t io_num;
    while (true)
    {
        if (xQueueReceive(go_sleep_evt_queue, &io_num, portMAX_DELAY))
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            go_into_deep_sleep();
        }
    }
}

static void set_led_task()
{
    while (true)
    {
        vTaskDelay(15000 / portTICK_PERIOD_MS);
        set_led();
        vTaskDelay(15000 / portTICK_PERIOD_MS);
    }
    
}

static void micro_blink_task()
{
    vTaskDelay(1000 / portTICK_RATE_MS);
    while (true)
    {
        gpio_set_level(GPIO_OUTPUT_STATUS_LED, 1);
        vTaskDelay(10 / portTICK_RATE_MS);
        gpio_set_level(GPIO_OUTPUT_STATUS_LED, 0);
        vTaskDelay(500 / portTICK_RATE_MS);
    }
}

static void auto_sleep_task()
{
    vTaskDelay(300000 / portTICK_RATE_MS);
    printf("Nothing done, sleeping to save power...\n");
    go_into_deep_sleep();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

// HTTP GET Handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    ESP_LOGI(TAG, "Serve root");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;
    time(&now);
    // Set timezone to Berlin Time
    //setenv("TZ", "GMT-1", 1);
    // setenv("TZ", "GMT+1", 1);
    //tzset();

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Berlin is: %s", strftime_buf);

    return ESP_OK;
}

static esp_err_t script_get_handler(httpd_req_t *req)
{
    const uint32_t script_len = script_end - script_start;

    ESP_LOGI(TAG, "Serve Script");
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, script_start, script_len);

    return ESP_OK;
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    const uint32_t style_len = style_end - style_start;

    ESP_LOGI(TAG, "Serve Style");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, style_start, style_len);

    return ESP_OK;
}

static esp_err_t ota_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "DO OTA Update");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(1000 / portTICK_RATE_MS);

    xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t sleep_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(1000 / portTICK_RATE_MS);

    go_into_deep_sleep();

    return ESP_OK;
}

static esp_err_t api_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    ESP_LOGI(TAG, "received %d size", total_len);
    int cur_len = 0;
    char *buf = malloc(sizeof(char) * total_len); //((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE)
    {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len)
    {
        ESP_LOGI(TAG, "CurLen: %d size", cur_len);
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0)
        {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "Buf: %s size", buf);
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    // Time Settings
    onHr = cJSON_GetObjectItem(root, "onHr")->valuestring;
    onMn = cJSON_GetObjectItem(root, "onMn")->valuestring;
    offHr = cJSON_GetObjectItem(root, "offHr")->valuestring;
    offMn = cJSON_GetObjectItem(root, "offMn")->valuestring;
    // Time Server Settings
    timeServer = cJSON_GetObjectItem(root, "timeServer")->valuestring;
    // AP Settings
    ssid = cJSON_GetObjectItem(root, "ssid")->valuestring;
    passwd = cJSON_GetObjectItem(root, "passwd")->valuestring;
    // OTA
    otaServer = cJSON_GetObjectItem(root, "otaServer")->valuestring;

    ESP_LOGI(TAG, "Values: onHr = %s, onMn = %s, offHr = %s, offMn = %s, timeServer = %s, ssid = %s, passwd = %s, otaServer = %s", onHr, onMn, offHr, offMn, timeServer, ssid, passwd, otaServer);

    // Time Settings
    nvs_set_str(nvs_handler, "onHr", onHr);
    nvs_set_str(nvs_handler, "onMn", onMn);
    nvs_set_str(nvs_handler, "offHr", offHr);
    nvs_set_str(nvs_handler, "offMn", offMn);
    // Time Server Settings
    nvs_set_str(nvs_handler, "timeServer", timeServer);
    // AP Settings
    nvs_set_str(nvs_handler, "ssid", ssid);
    nvs_set_str(nvs_handler, "passwd", passwd);
    // OTA
    nvs_set_str(nvs_handler, "otaServer", otaServer);

    httpd_resp_sendstr(req, "Post control value successfully");

    cJSON_Delete(root);

    // Time Settings
    length = 0;
    nvs_get_str(nvs_handler, "onHr", NULL, &length);
    ESP_LOGI(TAG, "length: %d", length);
    onHr = malloc(length);
    nvs_get_str(nvs_handler, "onHr", onHr, &length);
    length = 0;
    nvs_get_str(nvs_handler, "onMn", NULL, &length);
    onMn = malloc(length);
    nvs_get_str(nvs_handler, "onMn", onMn, &length);

    length = 0;
    nvs_get_str(nvs_handler, "offHr", NULL, &length);
    offHr = malloc(length);
    nvs_get_str(nvs_handler, "offHr", offHr, &length);
    length = 0;
    nvs_get_str(nvs_handler, "offMn", NULL, &length);
    offMn = malloc(length);
    nvs_get_str(nvs_handler, "offMn", offMn, &length);

    // Time Server Settings
    length = 0;
    nvs_get_str(nvs_handler, "timeServer", NULL, &length);
    timeServer = malloc(length);
    nvs_get_str(nvs_handler, "timeServer", timeServer, &length);

    // AP Settings
    length = 0;
    nvs_get_str(nvs_handler, "ssid", NULL, &length);
    ssid = malloc(length);
    nvs_get_str(nvs_handler, "ssid", ssid, &length);
    length = 0;
    nvs_get_str(nvs_handler, "passwd", NULL, &length);
    passwd = malloc(length);
    nvs_get_str(nvs_handler, "passwd", passwd, &length);

    // OTA
    length = 0;
    nvs_get_str(nvs_handler, "otaServer", NULL, &length);
    otaServer = malloc(length);
    nvs_get_str(nvs_handler, "otaServer", otaServer, &length);

    set_led();

    esp_restart();

    return ESP_OK;
}

static esp_err_t api_get_handler(httpd_req_t *req)
{
    char *stringResp = NULL;
    cJSON *resp = cJSON_CreateObject();
    // Time Settings
    cJSON_AddStringToObject(resp, "onHr", onHr);
    cJSON_AddStringToObject(resp, "onMn", onMn);
    cJSON_AddStringToObject(resp, "offHr", offHr);
    cJSON_AddStringToObject(resp, "offMn", offMn);
    // Time Server Settings
    cJSON_AddStringToObject(resp, "timeServer", timeServer);
    // AP Settings
    cJSON_AddStringToObject(resp, "ssid", ssid);
    cJSON_AddStringToObject(resp, "passwd", passwd);
    // OTA
    cJSON_AddStringToObject(resp, "otaServer", otaServer);

    stringResp = cJSON_Print(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, stringResp, HTTPD_RESP_USE_STRLEN);

    free(stringResp);
    cJSON_Delete(resp);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler};

static const httpd_uri_t script = {
    .uri = "/script.js",
    .method = HTTP_GET,
    .handler = script_get_handler};

static const httpd_uri_t style = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = style_get_handler};

static const httpd_uri_t ota = {
    .uri = "/ota",
    .method = HTTP_GET,
    .handler = ota_get_handler};

static const httpd_uri_t sleepy = {
    .uri = "/sleep",
    .method = HTTP_GET,
    .handler = sleep_get_handler};

static const httpd_uri_t apiget = {
    .uri = "/apiget",
    .method = HTTP_GET,
    .handler = api_get_handler};

static const httpd_uri_t api = {
    .uri = "/api",
    .method = HTTP_POST,
    .handler = api_post_handler};

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &script);
        httpd_register_uri_handler(server, &style);
        httpd_register_uri_handler(server, &ota);
        httpd_register_uri_handler(server, &sleepy);
        httpd_register_uri_handler(server, &api);
        httpd_register_uri_handler(server, &apiget);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
}

/* Initialize Wi-Fi as sta and set scan method */
static void wifi_scan(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    // esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    // assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++)
    {
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
        if (ap_info[i].authmode != WIFI_AUTH_WEP)
        {
        }
        ESP_LOGI(TAG, "Channel \t\t%d\n", ap_info[i].primary);
    }
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    /*
   char s[strlen(ssid)];
   memcpy(s, ssid, strlen(ssid));

   char p[strlen(passwd)];
   memcpy(p, passwd, strlen(passwd));

   wifi_config_t wifi_config = {
       .sta = {
           .ssid = {s},
           .password = {p},
       },
   };
   */

    printf("SSID: %s, length: %d\n", ssid, strlen(ssid));
    printf("Password: %s\n", passwd);
    if (strlen(ssid) < 1)
    {
        return false;
    }
    wifi_config_t wifi_config;

    bzero(&wifi_config, sizeof(wifi_config_t));
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, passwd, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ssid, passwd);
        return true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ssid, passwd);
        return false;
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return false;
    }
}

#define HASH_LEN 32

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
/* The interface name value can refer to if_desc in esp_netif_defaults.h */
#if CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF_ETH
static const char *bind_interface_name = "eth";
#elif CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF_STA
static const char *bind_interface_name = "sta";
#endif
#endif

#define OTA_URL_SIZE 256

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

void simple_ota_example_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA example");
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
    esp_netif_t *netif = get_example_netif_from_desc(bind_interface_name);
    if (netif == NULL)
    {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        abort();
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);
#endif
    esp_http_client_config_t config = {
        .url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
        .if_name = &ifr,
#endif
    };

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0)
    {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        esp_restart();
    }
    else
    {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void go_into_deep_sleep(void)
{
    printf("Going into Deep Sleep...\n");
    esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
}

void attach_interrupt_btn_sleep(void)
{
    printf("Attached BTN Interrupt\n");

    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    // change gpio interrupt type for one pin
    gpio_set_intr_type(GPIO_INPUT_BTN, GPIO_INTR_POSEDGE);

    gpio_isr_handler_add(GPIO_INPUT_BTN, go_sleep_isr_handler, (void *)GPIO_INPUT_BTN);
}

void initialize_GPIO(void)
{
    // zero-initialize the config structure.
    gpio_config_t io_conf = {};
    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 1;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);

    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    // bit mask of the pins that you want to set
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}

void blink_status_led(int n)
{
    for (size_t i = 0; i < n; i++)
    {
        gpio_set_level(GPIO_OUTPUT_STATUS_LED, 1);
        vTaskDelay(500 / portTICK_RATE_MS);
        gpio_set_level(GPIO_OUTPUT_STATUS_LED, 0);
        if ((i + 1) < n)
        {
            vTaskDelay(500 / portTICK_RATE_MS);
        }
    }
}

void micro_blink_status_led()
{
    xTaskCreate(micro_blink_task, "micro_blink_task", 2048, NULL, 10, NULL);
}

void auto_deep_sleep()
{
    xTaskCreate(auto_sleep_task, "auto_sleep_task", 2048, NULL, 10, NULL);
}

void button_press(void)
{
    initialize_GPIO();

    int starttime = esp_timer_get_time();
    bool setup = false;
    bool setupAp = false;
    bool reset = false;
    if (!setup)
    {
        setup = true;
        printf("Setup\n");
        blink_status_led(1);
    }
    while (gpio_get_level(GPIO_INPUT_BTN))
    {
        if ((esp_timer_get_time() - starttime) < 10000000 && (esp_timer_get_time() - starttime) > 5000000 && !setupAp)
        {
            setupAp = true;
            printf("Setup AP\n");
            blink_status_led(2);
        }
        if ((esp_timer_get_time() - starttime) > 30000000 && !reset)
        {
            reset = true;
            printf("Reset\n");
            blink_status_led(5);
        }

        vTaskDelay(1);
    }

    if (reset)
    {
        printf("Reset GO\n");
        restore_settings();
        esp_restart();
    }
    else if (setupAp)
    {
        printf("Setup AP GO\n");

        micro_blink_status_led();
        auto_deep_sleep();

        wifi_scan();

        // Create AP
        // Initialize networking stack
        ESP_ERROR_CHECK(esp_netif_init());

        // Create default event loop needed by the  main app
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Initialize Wi-Fi including netif with default config
        esp_netif_create_default_wifi_ap();

        // Initialise ESP32 in SoftAP mode
        wifi_init_softap();

        // Start the server for the first time
        start_webserver();

        // Start the DNS server that will redirect all queries to the softAP IP
        start_dns_server();

        attach_interrupt_btn_sleep();
    }
    else if (setup)
    {
        printf("Setup GO\n");

        micro_blink_status_led();
        auto_deep_sleep();

        // wifi_scan();

        if (wifi_init_sta())
        {
            // Sync with NTP
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, timeServer);
            sntp_setservername(1, "europe.pool.ntp.org");
            sntp_setservername(2, "uk.pool.ntp.org ");
            sntp_setservername(3, "us.pool.ntp.org");
            sntp_setservername(4, "time1.google.com");
            sntp_init();
            while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
            {
                vTaskDelay(1000 / portTICK_RATE_MS);
            }

            time_t now;
            char strftime_buf[64];
            struct tm timeinfo;
            time(&now);
            // Set timezone to Berlin Time
            //setenv("TZ", "GMT+1", 1);
            //tzset();

            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "The current date/time in Berlin is: %s", strftime_buf);

            // Start the server for the first time
            start_webserver();
        }
        else
        {
            // Scan for available APs to connect
            wifi_scan();

            // Create AP
            // Initialize networking stack
            ESP_ERROR_CHECK(esp_netif_init());

            // Create default event loop needed by the  main app
            // ESP_ERROR_CHECK(esp_event_loop_create_default());

            // Initialize Wi-Fi including netif with default config
            esp_netif_create_default_wifi_ap();

            // Initialise ESP32 in SoftAP mode
            wifi_init_softap();

            // Start the server for the first time
            start_webserver();

            // Start the DNS server that will redirect all queries to the softAP IP
            start_dns_server();
        }
        attach_interrupt_btn_sleep();
    }
}

void restore_settings(void)
{
    // Time Settings
    onHr = CONFIG_RTC_LIGHT_ON_HR;
    onMn = CONFIG_RTC_LIGHT_ON_MN;
    offHr = CONFIG_RTC_LIGHT_OFF_HR;
    offMn = CONFIG_RTC_LIGHT_OFF_MN;
    // Time Server Settings
    timeServer = CONFIG_RTC_LIGHT_TIME_SERVER;
    // AP Settings
    ssid = CONFIG_RTC_LIGHT_SSID;
    passwd = CONFIG_RTC_LIGHT_PASSWD;
    // OTA
    otaServer = CONFIG_RTC_LIGHT_OTA_SERVER;

    // Time Settings
    nvs_set_str(nvs_handler, "onHr", onHr);
    nvs_set_str(nvs_handler, "onMn", onMn);
    nvs_set_str(nvs_handler, "offHr", offHr);
    nvs_set_str(nvs_handler, "offMn", offMn);
    // Time Server Settings
    nvs_set_str(nvs_handler, "timeServer", timeServer);
    // AP Settings
    nvs_set_str(nvs_handler, "ssid", ssid);
    nvs_set_str(nvs_handler, "passwd", passwd);
    // OTA
    nvs_set_str(nvs_handler, "otaServer", otaServer);

    ESP_LOGI(TAG, "Settings restored!!! Values: onHr = %s, onMn = %s, offHr = %s, offMn = %s, timeServer = %s, ssid = %s, passwd = %s, otaServer = %s", onHr, onMn, offHr, offMn, timeServer, ssid, passwd, otaServer);
    // esp_restart();
}

void set_led(void){
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    int year = timeinfo.tm_year;
    int hour = timeinfo.tm_hour;
    int min = timeinfo.tm_min;


    ESP_LOGI(TAG, "Year: %d", year);
    if (year > 100) {
        initialize_GPIO();
        uint64_t sleepTime = 0;
        uint64_t onTimeNew = atoi(onHr)*60 + atoi(onMn);
        uint64_t offTimeNew = atoi(offHr)*60 + atoi(offMn);
        uint64_t curTimeNew = hour*60 + min;
        ESP_LOGI(TAG, "ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);

        if ((onTimeNew == curTimeNew) || (offTimeNew == curTimeNew))
        {
            curTimeNew++;
            ESP_LOGI(TAG, "Detected!");
        }

        if (onTimeNew == offTimeNew)
        {
            // gpio_set_level(GPIO_OUTPUT_MAIN_LED, 1);
            gpio_set_level(GPIO_OUTPUT_GND_MAIN, 1);
            ESP_LOGI(TAG, "Equal time detected");
        }
        else if (onTimeNew > offTimeNew)
        {
            if ((onTimeNew < curTimeNew && offTimeNew < curTimeNew) || (onTimeNew > curTimeNew && offTimeNew > curTimeNew))
            {
                // gpio_set_level(GPIO_OUTPUT_MAIN_LED, 1);
                gpio_set_level(GPIO_OUTPUT_GND_MAIN, 1);
                if (curTimeNew > offTimeNew)
                {
                    ESP_LOGI(TAG, "Next day on detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew + ((24*60) - curTimeNew)) * 60000000;
                }
                else
                {
                    ESP_LOGI(TAG, "Normal on detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew - curTimeNew) * 60000000;
                }
            }
            else
            {
                // gpio_set_level(GPIO_OUTPUT_MAIN_LED, 0);
                gpio_set_level(GPIO_OUTPUT_GND_MAIN, 0);
                ESP_LOGI(TAG, "This day off detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                if (curTimeNew > offTimeNew)
                {
                    ESP_LOGI(TAG, "Normal off detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew + ((24*60) - curTimeNew)) * 60000000;
                }
                else
                {
                    ESP_LOGI(TAG, "Normal off detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew - curTimeNew) * 60000000;
                }
            }
        }
        else
        {
            if (curTimeNew > onTimeNew && curTimeNew < offTimeNew)
            {
                // gpio_set_level(GPIO_OUTPUT_MAIN_LED, 1);
                gpio_set_level(GPIO_OUTPUT_GND_MAIN, 1);
                ESP_LOGI(TAG, "Normal on detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                sleepTime = (offTimeNew - curTimeNew) * 60000000;
            }
            else
            {
                // gpio_set_level(GPIO_OUTPUT_MAIN_LED, 0);
                gpio_set_level(GPIO_OUTPUT_GND_MAIN, 0);
                if (curTimeNew > offTimeNew)
                {
                    ESP_LOGI(TAG, "Normal off detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew + ((24*60) - curTimeNew)) * 60000000;
                }
                else
                {
                    ESP_LOGI(TAG, "Normal on detected; ON: %llu, OFF: %llu, CUR: %llu", onTimeNew, offTimeNew, curTimeNew);
                    sleepTime = (onTimeNew - curTimeNew) * 60000000;
                }
            }
        }
        if (sleepTime != 0)
        {
            ESP_LOGI(TAG, "Set sleepTime to: %llu", sleepTime);
            esp_sleep_enable_timer_wakeup(sleepTime);
        }
        else
        {
            ESP_LOGW(TAG, "sleepTime == 0");
        }
        
        //gpio_hold_en(GPIO_OUTPUT_MAIN_LED | GPIO_OUTPUT_GND_MAIN);
        gpio_hold_en(GPIO_OUTPUT_GND_MAIN);
        gpio_deep_sleep_hold_en();
    }
}

//////////////////////////////////////////////////////////App Main////////////////////////////////////////////////////////////////////////////////////
void app_main(void)
{
    setenv("TZ", "GMT-2", 1);
    tzset();
    // Things to do...
    // Set Modem to Power Save
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    // create a queue to handle gpio event from isr
    go_sleep_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Create Task for Interrupt
    xTaskCreate(go_sleep_task, "go_sleep_task", 2048, NULL, 10, NULL);

    // Initialize NVS Flash
    bool restore = false;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        restore = true;
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nvs_open("NVS_DATA", NVS_READWRITE, &nvs_handler));

    if (restore)
    {
        restore_settings();
    }

    // Time Settings
    length = 0;
    nvs_get_str(nvs_handler, "onHr", NULL, &length);
    ESP_LOGI(TAG, "length: %d", length);
    onHr = malloc(length);
    nvs_get_str(nvs_handler, "onHr", onHr, &length);
    length = 0;
    nvs_get_str(nvs_handler, "onMn", NULL, &length);
    onMn = malloc(length);
    nvs_get_str(nvs_handler, "onMn", onMn, &length);

    length = 0;
    nvs_get_str(nvs_handler, "offHr", NULL, &length);
    offHr = malloc(length);
    nvs_get_str(nvs_handler, "offHr", offHr, &length);
    length = 0;
    nvs_get_str(nvs_handler, "offMn", NULL, &length);
    offMn = malloc(length);
    nvs_get_str(nvs_handler, "offMn", offMn, &length);

    // Time Server Settings
    length = 0;
    nvs_get_str(nvs_handler, "timeServer", NULL, &length);
    timeServer = malloc(length);
    nvs_get_str(nvs_handler, "timeServer", timeServer, &length);

    // AP Settings
    length = 0;
    nvs_get_str(nvs_handler, "ssid", NULL, &length);
    ssid = malloc(length);
    nvs_get_str(nvs_handler, "ssid", ssid, &length);
    length = 0;
    nvs_get_str(nvs_handler, "passwd", NULL, &length);
    passwd = malloc(length);
    nvs_get_str(nvs_handler, "passwd", passwd, &length);

    // OTA
    length = 0;
    nvs_get_str(nvs_handler, "otaServer", NULL, &length);
    otaServer = malloc(length);
    nvs_get_str(nvs_handler, "otaServer", otaServer, &length);

    // length = 0;
    // nvs_get_str(nvs_handler, "WIFI_CONNECT", NULL, &length);
    // char *output = malloc(length);
    // nvs_get_str(nvs_handler, "WIFI_CONNECT", output, &length);
    ESP_LOGI(TAG, "ONHR!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!: %s", onHr);
    ESP_LOGI(TAG, "Values: onHr = %s, onMn = %s, offHr = %s, offMn = %s, timeServer = %s, ssid = %s, passwd = %s, otaServer = %s", onHr, onMn, offHr, offMn, timeServer, ssid, passwd, otaServer);

    // Create Task for led Calculation while uC is active
    // xTaskCreate(set_led_task, "set_led_task", 2048, NULL, 10, NULL);

    /*
        Turn of warnings from HTTP server as redirecting traffic will yield
        lots of invalid requests
    */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    // Main decision
    switch (esp_sleep_get_wakeup_cause())
    {
    // These should not exist:
    case ESP_SLEEP_WAKEUP_EXT0:
    {
        printf("Wake up from ext0\n");
        break;
    }
    case ESP_SLEEP_WAKEUP_GPIO:
    {
        printf("Wake up from GPIO\n");
        break;
    }
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
    {
        printf("Wake up from touch on pad %d\n", esp_sleep_get_touchpad_wakeup_status());
        break;
    }
    case ESP_SLEEP_WAKEUP_ULP:
    {
        printf("Wake up from ulp.\n");
        break;
    }

    // Triggert by Button press
    // IO needed
    // One time: Try to connect to WiFi else create AP; Create WEB, DNS & DHCP Server
    // Long press: Create AP and create WEB, DNS & DHCP Server
    // 30s press: Reset user input variables to default
    case ESP_SLEEP_WAKEUP_EXT1:
    {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask != 0)
        {
            int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
            printf("Wake up from GPIO %d\n", pin);
        }
        else
        {
            printf("Wake up from GPIO\n");
        }
        set_led();
        button_press();
        break;
    }
    // Triggert by ULP
    // Sync Time with NTP
    case ESP_SLEEP_WAKEUP_TIMER:
    {
        printf("Wake up from Timer\n");
        if (wifi_init_sta())
        {
            // Sync with NTP
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, timeServer);
            sntp_setservername(1, "europe.pool.ntp.org");
            sntp_setservername(2, "uk.pool.ntp.org ");
            sntp_setservername(3, "us.pool.ntp.org");
            sntp_setservername(4, "time1.google.com");
            sntp_init();
            while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
            {
                vTaskDelay(1000 / portTICK_RATE_MS);
            }
            set_led();

            time_t now;
            char strftime_buf[64];
            struct tm timeinfo;
            time(&now);
            /*
            // Set timezone to Berlin Time
            setenv("TZ", "GMT+1", 1);
            tzset();
            */

            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "The current date/time in Berlin is: %s", strftime_buf);

            // Sleep...
            go_into_deep_sleep();
        }
        break;
    }
    // Triggert on Startup / Reset
    // Try to connect to WiFi else create AP with WEB, DNS & DHCP Server
    // Sync Time with NTP if WiFi connection succeded
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    {
        printf("Wake up Undefined / Startup\n");
        if (wifi_init_sta())
        {
        
            // Sync with NTP
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, timeServer);
            sntp_setservername(1, "europe.pool.ntp.org");
            sntp_setservername(2, "uk.pool.ntp.org ");
            sntp_setservername(3, "us.pool.ntp.org");
            sntp_setservername(4, "time1.google.com");
            sntp_init();
            while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
            {
                vTaskDelay(1000 / portTICK_RATE_MS);
            }

            time_t now;
            char strftime_buf[64];
            struct tm timeinfo;
            time(&now);
            // Set timezone to Berlin Time
            //setenv("TZ", "GMT+1", 1);
            //tzset();
            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "The current date/time in Berlin is: %s", strftime_buf);
            set_led();

            
            // 4610468998145793006
            // Sleep...
            go_into_deep_sleep();
        }
        else
        {
            printf("Could not connect, creating AP\n");

            micro_blink_status_led();
            auto_deep_sleep();

            // Scan for available APs to connect
            wifi_scan();

            // Create AP
            // Initialize networking stack
            ESP_ERROR_CHECK(esp_netif_init());

            // Create default event loop needed by the  main app
            // ESP_ERROR_CHECK(esp_event_loop_create_default());

            // Initialize Wi-Fi including netif with default config
            esp_netif_create_default_wifi_ap();

            // Initialise ESP32 in SoftAP mode
            wifi_init_softap();

            // Start the server for the first time
            start_webserver();

            // Start the DNS server that will redirect all queries to the softAP IP
            start_dns_server();

            set_led();
            

            initialize_GPIO();
            attach_interrupt_btn_sleep();
        }
        break;
    }

    // Should also never be triggert
    default:
    {
        printf("Not a deep sleep reset\n");
        break;
    }
    }
}
