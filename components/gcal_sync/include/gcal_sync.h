/*
 * gcal_sync.h - Background Google Calendar synchronization via Google Apps Script.
 *
 * Fetches upcoming events over HTTPS from a user-supplied Google Apps Script
 * Web App URL (persisted in NVS or set via Kconfig / Web UI) and upserts them
 * into event_manager.
 */
#ifndef GCAL_SYNC_H
#define GCAL_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GCAL_URL_MAX_LEN 256

/** Load saved Google Apps Script URL from NVS (or Kconfig default). */
esp_err_t gcal_sync_init(void);

/** Copy the configured Google Calendar Web App URL into `out`. */
void gcal_sync_get_url(char *out, size_t outlen);

/** Save a new Google Calendar Web App URL to NVS and trigger an immediate sync. */
esp_err_t gcal_sync_set_url(const char *url);

/** Trigger an asynchronous background sync right now (non-blocking). */
void gcal_sync_trigger_now(void);

/** Periodic service called from main loop; runs auto-sync every 15 minutes. */
void gcal_sync_service(bool wifi_connected);

/** Short human-readable sync status for the Web UI and OLED. */
const char *gcal_sync_status(void);

#ifdef __cplusplus
}
#endif

#endif /* GCAL_SYNC_H */
