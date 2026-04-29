/*
 * Admin server — implementation.
 *
 * Embedded HTML+JS one-page admin. Compact but readable. Uses fetch() for
 * JSON state, posts URL-encoded forms for state changes (keeps the firmware
 * binary small).
 */

#include "app_admin.h"
#include "app_config.h"
#include "app_claude_api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "admin";

static httpd_handle_t s_httpd = NULL;

/* ------------------------------------------------------------------ */
/* HTML page                                                           */
/* ------------------------------------------------------------------ */

static const char ADMIN_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Claude Dashboard</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,system-ui,sans-serif;background:#0a0a0a;color:#e5e5e5;min-height:100vh;padding:16px;font-size:14px}"
".wrap{max-width:680px;margin:0 auto}"
"h1{color:#a78bfa;font-size:22px;margin-bottom:6px;font-weight:600}"
".sub{color:#777;font-size:12px;margin-bottom:18px}"
".card{background:#161616;border:1px solid #2a2a2a;border-radius:12px;padding:18px;margin-bottom:14px}"
"h2{font-size:14px;color:#aaa;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:12px;font-weight:500}"
".acct{padding:12px;background:#0d0d0d;border:1px solid #222;border-radius:8px;margin-bottom:8px;display:flex;align-items:center;gap:10px}"
".acct.active{border-color:#a78bfa}"
".acct .info{flex:1;min-width:0}"
".acct .lbl{font-weight:600;color:#fff}"
".acct .meta{color:#777;font-size:12px;margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
".acct .badge{display:inline-block;padding:1px 6px;border-radius:4px;font-size:10px;background:#333;color:#bbb;margin-left:6px}"
".acct .badge.oa{background:#5b21b6;color:#fff}"
".acct .badge.k{background:#1f2937;color:#aaa}"
".btn{padding:6px 10px;background:#222;color:#ddd;border:1px solid #333;border-radius:5px;font-size:12px;cursor:pointer;font-family:inherit}"
".btn:hover{background:#2a2a2a}"
".btn.act{background:#a78bfa;color:#000;border-color:#a78bfa}"
".btn.del{background:#1f1f1f;color:#ef4444;border-color:#3f1f1f}"
".row{display:flex;gap:8px}"
"label{display:block;font-size:11px;color:#888;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:4px;margin-top:10px}"
"input,select,textarea{width:100%;padding:9px 11px;background:#0d0d0d;border:1px solid #333;border-radius:5px;color:#fff;font-size:13px;font-family:inherit;outline:none}"
"input:focus,select:focus,textarea:focus{border-color:#a78bfa}"
"textarea{font-family:'SF Mono',Menlo,monospace;font-size:11px;resize:vertical;min-height:50px}"
"button.add{margin-top:14px;width:100%;padding:10px;background:#a78bfa;color:#000;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer}"
"button.add:hover{background:#8b5cf6}"
".inline{display:flex;gap:6px;align-items:center}"
".tabs{display:flex;gap:6px;margin-bottom:8px}"
".tab{padding:6px 14px;background:#1a1a1a;color:#888;border:1px solid #2a2a2a;border-radius:5px;cursor:pointer;font-size:12px}"
".tab.on{background:#a78bfa;color:#000;border-color:#a78bfa}"
".muted{color:#666;font-size:11px;margin-top:6px;line-height:1.4}"
".danger{background:#1a0d0d;border-color:#3f1f1f}"
".danger h2{color:#ef4444}"
".danger button{background:#7f1d1d;color:#fff;border-color:#7f1d1d}"
".empty{color:#666;font-style:italic;text-align:center;padding:14px}"
"</style></head><body><div class='wrap'>"
"<h1>Claude Usage Dashboard</h1>"
"<div class='sub' id='sub'>loading...</div>"

"<div class='card'><h2>Accounts</h2><div id='accts'></div></div>"

"<div class='card'><h2>Add account</h2>"
"<div class='tabs'>"
"<div class='tab on' onclick=\"setTab('oauth')\" id='tab_oauth'>OAuth</div>"
"<div class='tab' onclick=\"setTab('apikey')\" id='tab_apikey'>API key</div>"
"</div>"
"<form id='addform'>"
"<input type='hidden' name='type' id='atype' value='1'>"
"<label>Label</label>"
"<input name='label' required maxlength='31' placeholder='Personal / Work'>"
"<label>Email (optional)</label>"
"<input name='email' maxlength='63' placeholder='you@example.com'>"
"<div id='oauth_fields'>"
"<label>OAuth access token</label>"
"<textarea name='token' rows='3' placeholder='paste accessToken from ~/.claude/.credentials.json'></textarea>"
"<label>Refresh token</label>"
"<textarea name='refresh' rows='2' placeholder='paste refreshToken'></textarea>"
"<label>Expires at (unix ms)</label>"
"<input name='expires_ms' type='number' min='0' placeholder='0 = unknown'>"
"<div class='muted'>From <code>~/.claude/.credentials.json</code> → claudeAiOauth.{accessToken, refreshToken, expiresAt}</div>"
"</div>"
"<div id='apikey_fields' style='display:none'>"
"<label>API key</label>"
"<input name='apikey' maxlength='2000' placeholder='sk-ant-api03-...'>"
"<div class='muted'>From <a style='color:#a78bfa' href='https://console.anthropic.com/settings/keys'>console.anthropic.com</a></div>"
"</div>"
"<button class='add' type='submit'>Add account</button>"
"</form></div>"

"<div class='card'><h2>Auto-cycle</h2>"
"<form id='cycleform' class='inline'>"
"<input type='checkbox' name='enabled' id='cycle_en' style='width:auto'><label style='display:inline;margin:0 0 0 6px;text-transform:none'>Cycle through accounts every</label>"
"<input name='interval' type='number' min='5' max='600' style='width:80px;margin:0 6px'>seconds"
"<button class='btn act' type='submit' style='margin-left:auto'>Save</button>"
"</form></div>"

"<div class='card'><h2>Sleep schedule</h2>"
"<form id='sleepform' class='inline'>"
"<input type='checkbox' name='enabled' id='sleep_en' style='width:auto'><label style='display:inline;margin:0 0 0 6px;text-transform:none'>Dim display from</label>"
"<input name='start_h' type='number' min='0' max='23' style='width:60px;margin:0 6px'>:00 to "
"<input name='end_h' type='number' min='0' max='23' style='width:60px;margin:0 6px'>:00"
"<button class='btn act' type='submit' style='margin-left:auto'>Save</button>"
"</form></div>"

"<div class='card danger'><h2>Maintenance</h2>"
"<div class='inline'>"
"<button class='btn' onclick='reboot()'>Reboot</button>"
"<button class='btn' onclick='factory()'>Factory reset (clears WiFi + accounts)</button>"
"</div></div>"

"</div>"

"<script>"
"function setTab(t){"
"  document.getElementById('tab_oauth').classList.toggle('on',t==='oauth');"
"  document.getElementById('tab_apikey').classList.toggle('on',t==='apikey');"
"  document.getElementById('oauth_fields').style.display=t==='oauth'?'block':'none';"
"  document.getElementById('apikey_fields').style.display=t==='apikey'?'block':'none';"
"  document.getElementById('atype').value=t==='oauth'?1:0;"
"}"
"async function load(){"
"  const r=await fetch('/api/state');const s=await r.json();"
"  document.getElementById('sub').textContent=`${s.accounts.length} account(s) | active: ${s.active+1} | wifi: ${s.wifi_ssid}`;"
"  const box=document.getElementById('accts');box.innerHTML='';"
"  if(s.accounts.length===0){box.innerHTML='<div class=\"empty\">no accounts — add one below</div>';}"
"  s.accounts.forEach((a,i)=>{"
"    const div=document.createElement('div');div.className='acct'+(i===s.active?' active':'');"
"    const tb=a.type===1?'<span class=\"badge oa\">OAuth</span>':'<span class=\"badge k\">API key</span>';"
"    div.innerHTML=`<div class='info'><div class='lbl'>${escape_(a.label)} ${tb}</div>`"
"      +`<div class='meta'>${escape_(a.email||'(no email)')}</div></div>`"
"      +`<button class='btn ${i===s.active?'act':''}' onclick='setActive(${i})'>${i===s.active?'active':'use'}</button>`"
"      +`<button class='btn del' onclick='delAcc(${i})'>delete</button>`;"
"    box.appendChild(div);"
"  });"
"  document.getElementById('cycle_en').checked=s.cycle_enabled;"
"  document.querySelector('#cycleform [name=interval]').value=s.cycle_interval;"
"  document.getElementById('sleep_en').checked=s.sleep_enabled;"
"  document.querySelector('#sleepform [name=start_h]').value=s.sleep_start_h;"
"  document.querySelector('#sleepform [name=end_h]').value=s.sleep_end_h;"
"}"
"function escape_(s){return (s||'').replace(/[<>&\"']/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','\"':'&quot;',\"'\":'&#39;'}[c]));}"
"async function postf(url,data){const fd=new URLSearchParams(data);const r=await fetch(url,{method:'POST',body:fd});return r.json();}"
"async function setActive(i){await postf('/api/account/active',{idx:i});load();}"
"async function delAcc(i){if(!confirm('Delete account?'))return;await postf('/api/account/del',{idx:i});load();}"
"async function reboot(){if(!confirm('Reboot?'))return;await postf('/api/reboot',{});alert('Rebooting in 2s...');}"
"async function factory(){if(!confirm('Factory reset — clears WiFi + ALL accounts. Continue?'))return;await postf('/api/factory_reset',{});alert('Resetting...');}"
"document.getElementById('addform').addEventListener('submit',async e=>{"
"  e.preventDefault();const f=e.target;const fd=new FormData(f);"
"  const data={label:fd.get('label'),email:fd.get('email')||'',type:fd.get('type')};"
"  if(data.type==='1'){data.token=fd.get('token');data.refresh=fd.get('refresh');data.expires_ms=fd.get('expires_ms')||'0';}"
"  else{data.token=fd.get('apikey');data.refresh='';data.expires_ms='0';}"
"  if(!data.token){alert('Token required');return;}"
"  const res=await postf('/api/account/add',data);"
"  if(res.ok){f.reset();load();}else{alert(res.error||'failed');}"
"});"
"document.getElementById('cycleform').addEventListener('submit',async e=>{"
"  e.preventDefault();const fd=new FormData(e.target);"
"  await postf('/api/cycle',{enabled:document.getElementById('cycle_en').checked?1:0,interval:fd.get('interval')});load();"
"});"
"document.getElementById('sleepform').addEventListener('submit',async e=>{"
"  e.preventDefault();const fd=new FormData(e.target);"
"  await postf('/api/sleep',{enabled:document.getElementById('sleep_en').checked?1:0,start_h:fd.get('start_h'),end_h:fd.get('end_h')});load();"
"});"
"load();setInterval(load,5000);"
"</script></body></html>";

/* ------------------------------------------------------------------ */
/* form parsing                                                        */
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

/* Read full POST body into a heap-allocated buffer (caller frees). */
static char *recv_body(httpd_req_t *req, int max_len)
{
    int total = req->content_len;
    if (total <= 0 || total > max_len) return NULL;
    char *buf = malloc((size_t)total + 1);
    if (!buf) return NULL;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[got] = '\0';
    return buf;
}

/* Find ?key=value in a URL-encoded body. Decodes into out. Returns true if found. */
static bool form_get(const char *body, const char *name, char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", name);
    /* Walk all & boundaries to avoid prefix matches */
    const char *p = body;
    while (*p) {
        if ((p == body || *(p - 1) == '&') && strncmp(p, search, strlen(search)) == 0) {
            const char *start = p + strlen(search);
            const char *end = strchr(start, '&');
            size_t len = end ? (size_t)(end - start) : strlen(start);
            char enc[2200];
            if (len >= sizeof(enc)) len = sizeof(enc) - 1;
            memcpy(enc, start, len);
            enc[len] = '\0';
            url_decode(out, enc, out_size);
            return true;
        }
        p++;
    }
    out[0] = '\0';
    return false;
}

/* ------------------------------------------------------------------ */
/* Handlers                                                            */
/* ------------------------------------------------------------------ */

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ADMIN_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_state(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "active", app_config_get_active_index());
    cJSON_AddStringToObject(root, "wifi_ssid", app_config_get_wifi_ssid());

    cJSON *arr = cJSON_AddArrayToObject(root, "accounts");
    uint8_t cnt = app_config_get_account_count();
    for (uint8_t i = 0; i < cnt; i++) {
        app_account_t a;
        if (!app_config_get_account(i, &a)) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "label", a.label);
        cJSON_AddStringToObject(o, "email", a.email);
        cJSON_AddStringToObject(o, "tier", a.tier);
        cJSON_AddNumberToObject(o, "type", (double)a.type);
        cJSON_AddNumberToObject(o, "expires_ms", (double)a.expires_ms);
        cJSON_AddItemToArray(arr, o);
    }

    cJSON_AddBoolToObject(root, "cycle_enabled", app_config_get_cycle_enabled());
    cJSON_AddNumberToObject(root, "cycle_interval", app_config_get_cycle_interval());
    cJSON_AddBoolToObject(root, "sleep_enabled", app_config_get_sleep_enabled());
    cJSON_AddNumberToObject(root, "sleep_start_h", app_config_get_sleep_start_h());
    cJSON_AddNumberToObject(root, "sleep_end_h", app_config_get_sleep_end_h());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t h_acct_add(httpd_req_t *req)
{
    char *body = recv_body(req, 8192);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }

    app_account_t a = {0};
    char tbuf[12], expbuf[24];
    form_get(body, "label", a.label, sizeof(a.label));
    form_get(body, "email", a.email, sizeof(a.email));
    form_get(body, "token", a.token, sizeof(a.token));
    form_get(body, "refresh", a.refresh, sizeof(a.refresh));
    form_get(body, "type", tbuf, sizeof(tbuf));
    form_get(body, "expires_ms", expbuf, sizeof(expbuf));
    a.type = (atoi(tbuf) == 1) ? ACCT_TYPE_OAUTH : ACCT_TYPE_API_KEY;
    a.expires_ms = (int64_t)atoll(expbuf);
    free(body);

    if (a.label[0] == '\0' || a.token[0] == '\0') {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"label and token required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    uint8_t idx;
    esp_err_t err = app_config_add_account(&a, &idx);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"add failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Auto-fetch profile to populate tier (and email if blank) */
    char fetched_email[APP_EMAIL_MAX_LEN] = {0};
    char fetched_tier[APP_TIER_MAX_LEN]   = {0};
    if (app_claude_api_fetch_profile(idx, fetched_email, sizeof(fetched_email),
                                      fetched_tier, sizeof(fetched_tier)) == ESP_OK) {
        app_account_t loaded;
        if (app_config_get_account(idx, &loaded)) {
            if (loaded.email[0] == 0 && fetched_email[0]) {
                strncpy(loaded.email, fetched_email, sizeof(loaded.email) - 1);
            }
            if (fetched_tier[0]) {
                strncpy(loaded.tier, fetched_tier, sizeof(loaded.tier) - 1);
            }
            app_config_set_account(idx, &loaded);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_acct_del(httpd_req_t *req)
{
    char *body = recv_body(req, 256);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    char ibuf[12];
    form_get(body, "idx", ibuf, sizeof(ibuf));
    free(body);
    int idx = atoi(ibuf);
    if (idx < 0 || idx >= app_config_get_account_count()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad idx"); return ESP_FAIL;
    }
    app_config_delete_account((uint8_t)idx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_acct_active(httpd_req_t *req)
{
    char *body = recv_body(req, 256);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    char ibuf[12]; form_get(body, "idx", ibuf, sizeof(ibuf)); free(body);
    int idx = atoi(ibuf);
    if (app_config_set_active_index((uint8_t)idx) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad idx"); return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_cycle(httpd_req_t *req)
{
    char *body = recv_body(req, 256);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    char eb[8], ib[12];
    form_get(body, "enabled", eb, sizeof(eb));
    form_get(body, "interval", ib, sizeof(ib));
    free(body);
    bool en = (atoi(eb) != 0);
    int sec = atoi(ib);
    if (sec <= 0) sec = 30;
    app_config_set_cycle(en, (uint16_t)sec);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_sleep(httpd_req_t *req)
{
    char *body = recv_body(req, 256);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    char eb[8], sb[8], xb[8];
    form_get(body, "enabled", eb, sizeof(eb));
    form_get(body, "start_h", sb, sizeof(sb));
    form_get(body, "end_h",   xb, sizeof(xb));
    free(body);
    app_config_set_sleep(atoi(eb) != 0, (uint8_t)atoi(sb), (uint8_t)atoi(xb));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void delayed_reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    xTaskCreate(delayed_reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void delayed_factory_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    app_config_factory_reset();
}

static esp_err_t h_factory(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    xTaskCreate(delayed_factory_task, "factory", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

esp_err_t app_admin_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "httpd");

    httpd_uri_t routes[] = {
        { .uri = "/",                    .method = HTTP_GET,  .handler = h_root        },
        { .uri = "/api/state",           .method = HTTP_GET,  .handler = h_state       },
        { .uri = "/api/account/add",     .method = HTTP_POST, .handler = h_acct_add    },
        { .uri = "/api/account/del",     .method = HTTP_POST, .handler = h_acct_del    },
        { .uri = "/api/account/active",  .method = HTTP_POST, .handler = h_acct_active },
        { .uri = "/api/cycle",           .method = HTTP_POST, .handler = h_cycle       },
        { .uri = "/api/sleep",           .method = HTTP_POST, .handler = h_sleep       },
        { .uri = "/api/reboot",          .method = HTTP_POST, .handler = h_reboot      },
        { .uri = "/api/factory_reset",   .method = HTTP_POST, .handler = h_factory     },
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_httpd, &routes[i]);
    }

    ESP_LOGI(TAG, "Admin server listening on :80");
    return ESP_OK;
}

void app_admin_stop(void)
{
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
}
