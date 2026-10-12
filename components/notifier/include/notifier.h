/*
 * notifier.h - Optional push notifications for due events.
 *
 * Delivery is compiled in only when enabled in menuconfig, and is skipped at
 * runtime if the token/chat id is empty. Sends are synchronous and best-effort:
 * a failed request is logged and never aborts the caller (it may briefly block
 * for the HTTP timeout).
 */
#ifndef NOTIFIER_H
#define NOTIFIER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Log the notification configuration. Call once at startup. */
void notifier_init(void);

/** True if at least one channel is compiled in. */
bool notifier_enabled(void);

/** Send a reminder to every enabled channel (no-op if disabled/unconfigured). */
void notifier_send(const char *title, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* NOTIFIER_H */
