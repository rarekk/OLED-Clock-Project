/*
 * web_server.h - Tiny HTTP UI for managing calendar events from a phone.
 */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server on port 80 (call after the network is up). */
esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
