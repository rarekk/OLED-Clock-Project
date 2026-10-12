#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "notifier.h"

static const char *TAG = "notify";

bool notifier_enabled(void)
{
#if CONFIG_OLED_CLOCK_TELEGRAM_ENABLE || CONFIG_OLED_CLOCK_LINE_ENABLE
    return true;
#else
    return false;
#endif
}

#if CONFIG_OLED_CLOCK_TELEGRAM_ENABLE || CONFIG_OLED_CLOCK_LINE_ENABLE

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

/* Escape a string for use inside a JSON string literal. */
static void json_escape(const char *src, char *dst, size_t dstlen)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 3 < dstlen; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c < 0x20) {
            /* skip control characters */
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

/* Percent-encode a string for application/x-www-form-urlencoded bodies. */
static void url_encode(const char *src, char *dst, size_t dstlen)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 4 < dstlen; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0x0F];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

static void http_post(const char *url, const char *content_type,
                      const char *auth, const char *body)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    if (auth != NULL) {
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST ok (HTTP %d) %s", esp_http_client_get_status_code(client), url);
    } else {
        ESP_LOGW(TAG, "POST failed: %s (%s)", esp_err_to_name(err), url);
    }
    esp_http_client_cleanup(client);
}

#endif /* any channel enabled */

void notifier_init(void)
{
    if (!notifier_enabled()) {
        ESP_LOGI(TAG, "push notifications disabled");
        return;
    }
#if CONFIG_OLED_CLOCK_TELEGRAM_ENABLE
    ESP_LOGI(TAG, "Telegram channel %s",
             strlen(CONFIG_OLED_CLOCK_TELEGRAM_BOT_TOKEN) ? "configured" : "MISSING TOKEN");
#endif
#if CONFIG_OLED_CLOCK_LINE_ENABLE
    ESP_LOGI(TAG, "LINE Notify channel %s",
             strlen(CONFIG_OLED_CLOCK_LINE_TOKEN) ? "configured" : "MISSING TOKEN");
#endif
}

void notifier_send(const char *title, const char *message)
{
    if (!notifier_enabled()) {
        return;
    }

#if CONFIG_OLED_CLOCK_TELEGRAM_ENABLE || CONFIG_OLED_CLOCK_LINE_ENABLE
    char text[192];
    snprintf(text, sizeof(text), "%s: %s", title ? title : "Reminder",
             message ? message : "");
#endif

#if CONFIG_OLED_CLOCK_TELEGRAM_ENABLE
    if (strlen(CONFIG_OLED_CLOCK_TELEGRAM_BOT_TOKEN) > 0 &&
        strlen(CONFIG_OLED_CLOCK_TELEGRAM_CHAT_ID) > 0) {
        char url[256];
        char esc[256];
        char body[384];

        snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage",
                 CONFIG_OLED_CLOCK_TELEGRAM_BOT_TOKEN);
        json_escape(text, esc, sizeof(esc));
        snprintf(body, sizeof(body), "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                 CONFIG_OLED_CLOCK_TELEGRAM_CHAT_ID, esc);
        http_post(url, "application/json", NULL, body);
    } else {
        ESP_LOGW(TAG, "Telegram enabled but token/chat id not set");
    }
#endif

#if CONFIG_OLED_CLOCK_LINE_ENABLE
    if (strlen(CONFIG_OLED_CLOCK_LINE_TOKEN) > 0) {
        char encoded[512];
        char body[576];
        char auth[160];

        url_encode(text, encoded, sizeof(encoded));
        snprintf(body, sizeof(body), "message=%s", encoded);
        snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_OLED_CLOCK_LINE_TOKEN);
        http_post("https://notify-api.line.me/api/notify",
                  "application/x-www-form-urlencoded", auth, body);
    } else {
        ESP_LOGW(TAG, "LINE enabled but token not set");
    }
#endif
}
