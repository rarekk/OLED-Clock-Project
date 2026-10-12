#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_http_server.h"

#include "event_manager.h"
#include "web_server.h"

static const char *TAG = "web";

#define BODY_MAX 320

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

/* ---------------- page ---------------- */

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>OLED Clock - Events</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:16px;max-width:560px}"
    "input,button{font-size:16px;padding:8px;margin:4px 0;box-sizing:border-box}"
    "input[type=text]{width:100%}"
    "button{background:#2d7;border:0;border-radius:6px;color:#fff;padding:10px 16px}"
    "table{width:100%;border-collapse:collapse;margin-top:8px}"
    "td,th{border-bottom:1px solid #ccc;padding:8px;text-align:left}"
    "a.del{color:#c00;text-decoration:none}</style></head><body>"
    "<h2>OLED Clock &ndash; Events</h2>"
    "<form method='POST' action='/add'>"
    "<input type='text' name='title' maxlength='31' placeholder='Event title' required>"
    "<input type='date' name='date' required>"
    "<input type='time' name='time' required>"
    "<button type='submit'>Add event</button>"
    "</form>";

static const char PAGE_TAIL[] =
    "<p><a href='/clear'>Clear all events</a></p>"
    "</body></html>";

static esp_err_t send_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<table><tr><th>When</th><th>Title</th><th></th></tr>");

    char row[256];
    char safe[96];
    int n = event_manager_count();

    for (int i = 0; i < n; i++) {
        const calendar_event_t *e = event_manager_get(i);
        if (e == NULL) continue;
        html_escape(e->title, safe, sizeof(safe));
        snprintf(row, sizeof(row),
                 "<tr><td>%04u-%02u-%02u<br>%02u:%02u</td><td>%s</td>"
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

    /* Redirect back to the list. */
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

/* ---------------- start ---------------- */

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/add",    .method = HTTP_POST, .handler = add_post_handler },
        { .uri = "/delete", .method = HTTP_GET,  .handler = delete_get_handler },
        { .uri = "/clear",  .method = HTTP_GET,  .handler = clear_get_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "web server started on http://<device-ip>/");
    return ESP_OK;
}
