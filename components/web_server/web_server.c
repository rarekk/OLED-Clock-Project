#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_sntp.h"

#include "event_manager.h"
#include "gcal_sync.h"
#include "buzzer.h"
#include "web_server.h"

static const char *TAG = "web";

#define BODY_MAX 512

/* ---------------- helpers ---------------- */

static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], '\0' };
            *o++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

/* Extract a key=value pair from an urlencoded body into out (decoded). */
static bool form_get(const char *body, const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = body;

    while ((p = strstr(p, key)) != NULL) {
        if (p == body || p[-1] == '&') {
            const char *eq = p + klen;
            if (*eq != '=') {
                p += klen;
                continue;
            }
            eq++;
            const char *end = strchr(eq, '&');
            size_t len = end ? (size_t)(end - eq) : strlen(eq);
            if (len >= outlen) len = outlen - 1;
            memcpy(out, eq, len);
            out[len] = '\0';
            url_decode(out);
            return true;
        }
        p += klen;
    }
    return false;
}

static void html_escape(const char *src, char *dst, size_t dstlen)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < dstlen; i++) {
        switch (src[i]) {
        case '<':  memcpy(&dst[j], "&lt;", 4);  j += 4; break;
        case '>':  memcpy(&dst[j], "&gt;", 4);  j += 4; break;
        case '&':  memcpy(&dst[j], "&amp;", 5); j += 5; break;
        case '"':  memcpy(&dst[j], "&quot;", 6); j += 6; break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

/* ---------------- Pokemon Center Theme Dashboard ---------------- */

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Pokemon Center Clock Dashboard</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#fff5f5;color:#1e293b;padding-bottom:32px}"
    ".pokebar{background:linear-gradient(180deg,#e3350d 0%,#c62828 100%);color:#fff;padding:18px 16px 26px;text-align:center;border-bottom:6px solid #1e293b;position:relative;box-shadow:0 4px 12px rgba(227,53,13,.25)}"
    ".pokebar h1{margin:0;font-size:22px;letter-spacing:.5px;text-shadow:0 2px 0 #7f1d1d}"
    ".pokebar p{margin:4px 0 0;font-size:13px;opacity:.92}"
    ".pokeball-btn{width:44px;height:44px;border-radius:50%;background:#fff;border:5px solid #1e293b;position:absolute;left:50%;bottom:-25px;transform:translateX(-50%);display:flex;align-items:center;justify-content:center;cursor:pointer;box-shadow:0 2px 6px rgba(0,0,0,.25)}"
    ".pokeball-btn::after{content:'';width:16px;height:16px;border-radius:50%;background:#e3350d;border:2px solid #1e293b}"
    ".wrap{max-width:580px;margin:34px auto 0;padding:0 14px}"
    ".card{background:#fff;border:2px solid #fecaca;border-top:5px solid #e3350d;border-radius:14px;padding:15px;margin-bottom:16px;box-shadow:0 3px 10px rgba(227,53,13,.08)}"
    "h3{margin:0 0 10px;color:#b91c1c;display:flex;align-items:center;justify-content:space-between;font-size:17px}"
    ".badge{background:#fee2e2;color:#b91c1c;padding:3px 9px;border-radius:99px;font-size:12px;font-weight:700}"
    "input,button,select{font-size:15px;padding:9px 11px;margin:4px 0;border-radius:8px;border:1.5px solid #fca5a5}"
    "input[type=text]{width:100%}"
    "button,.btn{background:#e3350d;border:2px solid #991b1b;color:#fff;padding:9px 15px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block;border-radius:8px;transition:.15s}"
    "button:hover,.btn:hover{background:#b91c1c}"
    ".btn-sub{background:#fff;color:#e3350d;border:2px solid #e3350d}"
    ".btn-sub:hover{background:#fef2f2}"
    ".music-bar{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
    "table{width:100%;border-collapse:collapse;margin-top:10px}"
    "th{background:#fef2f2;color:#991b1b;font-size:13px;padding:8px;text-align:left;border-bottom:2px solid #fecaca}"
    "td{border-bottom:1px solid #fee2e2;padding:8px;font-size:14px}"
    "a.del{color:#e3350d;text-decoration:none;font-weight:700;padding:3px 8px;border:1px solid #fecaca;border-radius:6px;background:#fff5f5}"
    "</style></head><body>"
    "<div class='pokebar'>"
    "<h1>&#9889; POK&Eacute;MON CENTER CLOCK</h1>"
    "<p>ESP32 OLED Desk Clock &bull; Control &amp; Sync Center</p>"
    "<div class='pokeball-btn' onclick='toggleBgm()' title='Play/Stop Pokemon Center BGM'></div>"
    "</div>"
    "<div class='wrap'>"
    "<div class='card'><h3><span>&#127925; Pok&eacute;mon Center Sound Center</span><span class='badge' id='bgm-state'>BGM OFF</span></h3>"
    "<div class='music-bar'>"
    "<button type='button' onclick='toggleBgm()' id='bgm-btn'>&#9654; Play Web BGM</button>"
    "<a class='btn btn-sub' href='/buzzer?cmd=heal'>&#127973; Heal Jingle (ESP32)</a>"
    "<a class='btn btn-sub' href='/buzzer?cmd=play'>&#128276; Test Alarm Melody</a>"
    "<a class='btn btn-sub' href='/buzzer?cmd=stop'>&#128263; Stop Buzzer</a>"
    "</div></div>";

static const char EVENT_FORM[] =
    "<div class='card'><h3><span>&#128197; Pok&eacute;Center Appointments</span></h3>"
    "<form method='POST' action='/add'>"
    "<input type='text' name='title' maxlength='31' placeholder='Event title (ASCII for OLED, e.g. GYM BATTLE)' required>"
    "<input type='date' name='date' required> "
    "<input type='time' name='time' required> "
    "<button type='submit'>+ Add Event</button>"
    "</form>";

static const char PAGE_TAIL[] =
    "<p style='margin-top:12px'><a class='del' href='/clear'>Clear all events</a></p></div></div>"
    "<script>"
    "const NOTES=["
    "[1175,160],[880,160],[1175,160],[1760,320],[1568,320],[1480,160],[1319,160],[1109,460],[0,140],"
    "[1109,160],[880,160],[1109,160],[1480,320],[1319,320],[1109,160],[1175,160],[1480,160],[1760,460],[0,140],"
    "[1175,160],[880,160],[1175,160],[1760,320],[1568,320],[1480,160],[1319,160],[1568,460],[0,120],"
    "[1480,160],[1319,160],[1175,160],[1109,160],[1175,480],[0,220],"
    "[1976,140],[0,30],[1976,140],[0,30],[1976,140],[0,30],[1661,140],[0,30],[2637,420],[0,350]"
    "];"
    "let ctx=null,playing=false,timer=null,idx=0;"
    "function step(){"
    "if(!playing)return;"
    "const [f,d]=NOTES[idx];idx=(idx+1)%NOTES.length;"
    "if(f>0){"
    "const o=ctx.createOscillator(),g=ctx.createGain();"
    "o.type='square';o.frequency.value=f/2;"
    "g.gain.setValueAtTime(0.08,ctx.currentTime);"
    "g.gain.exponentialRampToValueAtTime(0.008,ctx.currentTime+d/1000);"
    "o.connect(g);g.connect(ctx.destination);"
    "o.start();o.stop(ctx.currentTime+d/1000);"
    "}"
    "timer=setTimeout(step,d);"
    "}"
    "function toggleBgm(){"
    "if(!ctx)ctx=new(window.AudioContext||window.webkitAudioContext)();"
    "playing=!playing;"
    "document.getElementById('bgm-state').textContent=playing?'BGM PLAYING':'BGM OFF';"
    "document.getElementById('bgm-btn').innerHTML=playing?'&#9209; Stop Web BGM':'&#9654; Play Web BGM';"
    "if(playing){idx=0;step();}else if(timer){clearTimeout(timer);}"
    "}"
    "function syncNow(){"
    "const n=new Date(),p=v=>String(v).padStart(2,'0');"
    "document.getElementById('dt-d').value=`${n.getFullYear()}-${p(n.getMonth()+1)}-${p(n.getDate())}`;"
    "document.getElementById('dt-t').value=`${p(n.getHours())}:${p(n.getMinutes())}:${p(n.getSeconds())}`;"
    "document.getElementById('dt-f').submit();"
    "}"
    "</script></body></html>";

static esp_err_t send_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);

    time_t now = 0;
    struct tm ti = { 0 };
    time(&now);
    localtime_r(&now, &ti);
    int yr = ti.tm_year + 1900;
    bool valid = (yr >= 2024);
    int disp_y  = valid ? yr : 2026;
    int disp_mo = valid ? (ti.tm_mon + 1) : 1;
    int disp_d  = valid ? ti.tm_mday : 1;
    int disp_h  = valid ? ti.tm_hour : 12;
    int disp_mi = valid ? ti.tm_min  : 0;
    int disp_s  = valid ? ti.tm_sec  : 0;

    char dt_card[720];
    snprintf(dt_card, sizeof(dt_card),
             "<div class='card'><h3><span>&#128344; Set Date &amp; Time</span>"
             "<span class='badge'>%04u-%02u-%02u %02u:%02u:%02u</span></h3>"
             "<form id='dt-f' method='POST' action='/set_time'>"
             "<input type='hidden' id='dt-ntp' name='ntp' value='0'>"
             "<input type='date' id='dt-d' name='date' value='%04u-%02u-%02u' required> "
             "<input type='time' id='dt-t' name='time' step='1' value='%02u:%02u:%02u' required> "
             "<button type='submit'>Save Date &amp; Time</button> "
             "<button type='button' class='btn-sub' onclick='syncNow()'>&#128241; Sync Phone/PC</button> "
             "<button type='button' class='btn-sub' onclick=\"document.getElementById('dt-ntp').value='1';document.getElementById('dt-f').submit();\">&#127760; Sync NTP</button>"
             "</form></div>",
             (unsigned)(disp_y % 10000), (unsigned)(disp_mo % 100), (unsigned)(disp_d % 100),
             (unsigned)(disp_h % 100), (unsigned)(disp_mi % 100), (unsigned)(disp_s % 100),
             (unsigned)(disp_y % 10000), (unsigned)(disp_mo % 100), (unsigned)(disp_d % 100),
             (unsigned)(disp_h % 100), (unsigned)(disp_mi % 100), (unsigned)(disp_s % 100));
    httpd_resp_sendstr_chunk(req, dt_card);

    alarm_config_t al = { 0 };
    event_manager_get_alarm(&al);
    char alarm_card[480];
    snprintf(alarm_card, sizeof(alarm_card),
             "<div class='card'><h3><span>&#9200; Daily Alarm (Pok&eacute;Center Theme)</span>"
             "<span class='badge'>%s</span></h3>"
             "<form method='POST' action='/alarm'>"
             "<input type='time' name='time' value='%02u:%02u' required> "
             "<select name='en'><option value='1'%s>ENABLED</option>"
             "<option value='0'%s>MUTED</option></select> "
             "<button type='submit'>Save Alarm</button>"
             "</form></div>",
             al.enabled ? "ARMED" : "OFF",
             al.hour, al.minute,
             al.enabled ? " selected" : "",
             al.enabled ? "" : " selected");
    httpd_resp_sendstr_chunk(req, alarm_card);

    char gcal_url[GCAL_URL_MAX_LEN] = { 0 };
    char safe_url[GCAL_URL_MAX_LEN + 32] = { 0 };
    gcal_sync_get_url(gcal_url, sizeof(gcal_url));
    html_escape(gcal_url, safe_url, sizeof(safe_url));

    char gcal_card[700];
    snprintf(gcal_card, sizeof(gcal_card),
             "<div class='card'><h3><span>&#9729; Google Calendar Auto-Sync</span>"
             "<span class='badge'>%s</span></h3>"
             "<form method='POST' action='/gcal'>"
             "<input type='text' name='url' maxlength='250' value='%s' "
             "placeholder='https://script.google.com/macros/s/.../exec'>"
             "<button type='submit'>Save &amp; Sync Now</button>"
             "</form></div>",
             gcal_sync_status(), safe_url);
    httpd_resp_sendstr_chunk(req, gcal_card);

    httpd_resp_sendstr_chunk(req, EVENT_FORM);
    httpd_resp_sendstr_chunk(req, "<table><tr><th>When</th><th>Title</th><th></th></tr>");

    char row[256];
    char safe[96];
    int n = event_manager_count();

    for (int i = 0; i < n; i++) {
        const calendar_event_t *e = event_manager_get(i);
        if (e == NULL) continue;
        html_escape(e->title, safe, sizeof(safe));
        snprintf(row, sizeof(row),
                 "<tr><td>%04u-%02u-%02u<br>%02u:%02u</td><td><b>%s</b></td>"
                 "<td><a class='del' href='/delete?i=%d'>del</a></td></tr>",
                 e->year, e->month, e->day, e->hour, e->minute, safe, i);
        httpd_resp_sendstr_chunk(req, row);
    }

    if (n == 0) {
        httpd_resp_sendstr_chunk(req, "<tr><td colspan='3'>No events yet.</td></tr>");
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ---------------- handlers ---------------- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    return send_page(req);
}

static esp_err_t add_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char title[EVENT_MANAGER_TITLE_LEN] = { 0 };
    char date[24] = { 0 };
    char time_str[16] = { 0 };

    if (!form_get(body, "title", title, sizeof(title)) ||
        !form_get(body, "date", date, sizeof(date)) ||
        !form_get(body, "time", time_str, sizeof(time_str))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }

    unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (sscanf(date, "%u-%u-%u", &y, &mo, &d) != 3 ||
        sscanf(time_str, "%u:%u", &h, &mi) != 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad date/time");
        return ESP_FAIL;
    }

    calendar_event_t ev = {
        .year = (uint16_t)y,
        .month = (uint8_t)mo,
        .day = (uint8_t)d,
        .hour = (uint8_t)h,
        .minute = (uint8_t)mi,
        .active = true,
    };
    strlcpy(ev.title, title, sizeof(ev.title));

    esp_err_t err = event_manager_add(&ev);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not add event");
        return ESP_FAIL;
    }

    buzzer_confirm();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t delete_get_handler(httpd_req_t *req)
{
    char query[64];
    char val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "i", val, sizeof(val)) == ESP_OK) {
        event_manager_remove(atoi(val));
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t clear_get_handler(httpd_req_t *req)
{
    event_manager_clear();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t alarm_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char time_str[16] = { 0 };
    char en_str[8] = { 0 };
    unsigned h = 0, mi = 0;

    if (!form_get(body, "time", time_str, sizeof(time_str)) ||
        sscanf(time_str, "%u:%u", &h, &mi) != 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad time");
        return ESP_FAIL;
    }
    form_get(body, "en", en_str, sizeof(en_str));

    alarm_config_t cfg = {
        .hour    = (uint8_t)(h % 24),
        .minute  = (uint8_t)(mi % 60),
        .enabled = (atoi(en_str) != 0),
    };
    event_manager_set_alarm(&cfg);
    buzzer_confirm();

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t gcal_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received > 0) {
        body[received] = '\0';
        char url[GCAL_URL_MAX_LEN] = { 0 };
        if (form_get(body, "url", url, sizeof(url))) {
            gcal_sync_set_url(url);
        }
    } else {
        gcal_sync_trigger_now();
    }
    buzzer_confirm();

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t buzzer_get_handler(httpd_req_t *req)
{
    char query[64];
    char cmd[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "cmd", cmd, sizeof(cmd)) == ESP_OK) {
        if (strcmp(cmd, "play") == 0) {
            buzzer_alarm_start();
        } else if (strcmp(cmd, "stop") == 0) {
            buzzer_alarm_stop();
        } else if (strcmp(cmd, "heal") == 0) {
            buzzer_alarm_stop();
            buzzer_confirm();
        }
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t set_time_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ntp_str[8] = { 0 };
    if (form_get(body, "ntp", ntp_str, sizeof(ntp_str)) && atoi(ntp_str) == 1) {
        esp_sntp_restart();
        buzzer_confirm();
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    char date_str[24] = { 0 };
    char time_str[16] = { 0 };
    if (!form_get(body, "date", date_str, sizeof(date_str)) ||
        !form_get(body, "time", time_str, sizeof(time_str))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing date/time");
        return ESP_FAIL;
    }

    unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (sscanf(date_str, "%u-%u-%u", &y, &mo, &d) != 3 ||
        sscanf(time_str, "%u:%u:%u", &h, &mi, &s) < 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad date/time");
        return ESP_FAIL;
    }

    struct tm tm_val = {
        .tm_year  = (int)y - 1900,
        .tm_mon   = (int)mo - 1,
        .tm_mday  = (int)d,
        .tm_hour  = (int)(h % 24),
        .tm_min   = (int)(mi % 60),
        .tm_sec   = (int)(s % 60),
        .tm_isdst = -1,
    };
    time_t epoch = mktime(&tm_val);
    if (epoch != (time_t)-1) {
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }
    buzzer_confirm();

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------------- start ---------------- */

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/add",      .method = HTTP_POST, .handler = add_post_handler },
        { .uri = "/alarm",    .method = HTTP_POST, .handler = alarm_post_handler },
        { .uri = "/set_time", .method = HTTP_POST, .handler = set_time_post_handler },
        { .uri = "/gcal",     .method = HTTP_POST, .handler = gcal_post_handler },
        { .uri = "/buzzer",   .method = HTTP_GET,  .handler = buzzer_get_handler },
        { .uri = "/delete",   .method = HTTP_GET,  .handler = delete_get_handler },
        { .uri = "/clear",    .method = HTTP_GET,  .handler = clear_get_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "web server started on http://<device-ip>/");
    return ESP_OK;
}
