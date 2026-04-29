/*
 * Captive Portal — WiFi setup HTML form + DNS catch-all on AP.
 * Adapted from bitaxe-ref/app_portal.c.
 */

#include "app_portal.h"
#include "app_config.h"
#include "app_wifi.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "portal";

#define DNS_PORT 53

static httpd_handle_t s_httpd = NULL;
static TaskHandle_t   s_dns_task = NULL;
static int            s_dns_sock = -1;

/* ------------------------------------------------------------------ */
/* HTML pages                                                          */
/* ------------------------------------------------------------------ */

static const char SETUP_PAGE_HTML[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Claude Dashboard Setup</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,system-ui,sans-serif;background:#0a0a0a;color:#e0e0e0;"
"min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}"
".card{background:#161616;border:1px solid #2a2a2a;border-radius:14px;padding:24px;"
"width:100%;max-width:380px;box-shadow:0 0 24px rgba(124,58,237,0.15)}"
"h1{font-size:20px;text-align:center;margin-bottom:6px;color:#a78bfa}"
".sub{text-align:center;font-size:12px;color:#777;margin-bottom:18px}"
"label{display:block;font-size:12px;color:#999;margin-bottom:4px;margin-top:14px;"
"text-transform:uppercase;letter-spacing:0.05em}"
"input,select{width:100%;padding:10px 12px;background:#0d0d0d;border:1px solid #333;"
"border-radius:6px;color:#fff;font-size:15px;outline:none;font-family:inherit}"
"input:focus,select:focus{border-color:#a78bfa}"
"button{width:100%;padding:12px;margin-top:20px;background:#a78bfa;color:#000;"
"border:none;border-radius:6px;font-size:15px;font-weight:600;cursor:pointer}"
"button:active{background:#8b5cf6}"
".note{text-align:center;font-size:11px;color:#555;margin-top:12px}"
"</style></head><body>"
"<div class='card'>"
"<h1>Claude Dashboard</h1>"
"<div class='sub'>Step 1 of 2 — connect to your WiFi</div>"
"<form method='POST' action='/save'>"
"<label>WiFi SSID</label>"
"<input name='ssid' required maxlength='32' placeholder='Your home network'>"
"<label>WiFi Password</label>"
"<input name='pass' type='password' maxlength='64' placeholder='WPA2 password'>"
"<button type='submit'>Save and reboot</button>"
"</form>"
"<p class='note'>After reboot, connect device's web admin at "
"<code>http://claude-dashboard.local</code> to add a Claude account.</p>"
"</div></body></html>";

static const char SAVED_PAGE_HTML[] =
"<!DOCTYPE html>"
"<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<meta http-equiv='refresh' content='3'>"
"<title>Saved</title>"
"<style>body{font-family:sans-serif;background:#0a0a0a;color:#e0e0e0;"
"display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center}"
"</style></head><body>"
"<div><h2 style='color:#4ade80'>Settings saved</h2>"
"<p style='margin-top:12px;color:#999'>Rebooting...</p></div></body></html>";

/* ------------------------------------------------------------------ */
/* URL form decode                                                     */
/* ------------------------------------------------------------------ */

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t di = 0;
    while (*src && di < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' '; src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

static bool form_get_field(const char *body, const char *name, char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", name);
    const char *start = strstr(body, search);
    if (!start) { out[0] = '\0'; return false; }
    start += strlen(search);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    char encoded[256];
    if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
    memcpy(encoded, start, len);
    encoded[len] = '\0';
    url_decode(out, encoded, out_size);
    return true;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_save(httpd_req_t *req)
{
    char body[512];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[APP_SSID_MAX_LEN] = {0};
    char pass[APP_PASS_MAX_LEN] = {0};

    if (!form_get_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    form_get_field(body, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "Saving WiFi config: SSID='%s'", ssid);
    app_config_set_wifi(ssid, pass);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_PAGE_HTML, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK; /* unreachable */
}

static esp_err_t handler_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* DNS server — answers all queries with 192.168.4.1                   */
/* ------------------------------------------------------------------ */

static void dns_task(void *arg)
{
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) { vTaskDelete(NULL); return; }
    if (bind(s_dns_sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        close(s_dns_sock); s_dns_sock = -1; vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "DNS started on :%d", DNS_PORT);

    uint8_t buf[512];
    struct sockaddr_in cli;
    socklen_t addr_len;
    uint8_t ap_ip[4] = {192, 168, 4, 1};

    while (1) {
        addr_len = sizeof(cli);
        int len = recvfrom(s_dns_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&cli, &addr_len);
        if (len < 12) continue;
        buf[2] = 0x81; buf[3] = 0x80;
        buf[6] = 0x00; buf[7] = 0x01;

        int qname_end = 12;
        while (qname_end < len && buf[qname_end] != 0) qname_end += buf[qname_end] + 1;
        qname_end++;
        int question_end = qname_end + 4;
        if (question_end > len) continue;

        int pos = question_end;
        if (pos + 16 > (int)sizeof(buf)) continue;
        buf[pos++] = 0xC0; buf[pos++] = 0x0C;
        buf[pos++] = 0x00; buf[pos++] = 0x01;
        buf[pos++] = 0x00; buf[pos++] = 0x01;
        buf[pos++] = 0x00; buf[pos++] = 0x00;
        buf[pos++] = 0x00; buf[pos++] = 0x0A;
        buf[pos++] = 0x00; buf[pos++] = 0x04;
        memcpy(&buf[pos], ap_ip, 4); pos += 4;

        sendto(s_dns_sock, buf, pos, 0, (struct sockaddr *)&cli, addr_len);
    }
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

esp_err_t app_portal_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 10;
    cfg.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "httpd");

    httpd_uri_t root = { .uri = "/",     .method = HTTP_GET,  .handler = handler_root };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = handler_save };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);

    const char *redirects[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/connecttest.txt", "/redirect", "/ncsi.txt"
    };
    for (size_t i = 0; i < sizeof(redirects)/sizeof(redirects[0]); i++) {
        httpd_uri_t u = { .uri = redirects[i], .method = HTTP_GET, .handler = handler_redirect };
        httpd_register_uri_handler(s_httpd, &u);
    }

    xTaskCreate(dns_task, "dns", 4096, NULL, 5, &s_dns_task);

    ESP_LOGI(TAG, "Portal ready on http://192.168.4.1/");
    return ESP_OK;
}

void app_portal_stop(void)
{
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    if (s_dns_sock >= 0) { close(s_dns_sock); s_dns_sock = -1; }
    if (s_dns_task) { vTaskDelete(s_dns_task); s_dns_task = NULL; }
}
