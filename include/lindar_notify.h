#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Graph node or borrowed adapter; release owned nodes with NodeFree and borrowed views through their
 * owner.
 */
typedef struct LND_NODE LND_NODE;

/** Owned notification queue and optional callback; survives owner detachment until SubscriptionFree. */
typedef struct LND_SUBSCRIPTION LND_SUBSCRIPTION;

enum {
    LND_NOTIFY_END = 0, /**< This owner reached EOF; downstream processing may still have a tail. */
    LND_NOTIFY_POSITION = 1, /**< Playback reached the configured frame position. */
    LND_NOTIFY_SLIDE_END = 2, /**< A parameter slide reached its target. */
    LND_NOTIFY_STALLED = 3, /**< Playback began waiting for input. */
    LND_NOTIFY_RESUMED = 4, /**< Playback resumed after waiting. */
};

enum {
    LND_NOTIFY_ONCE = 1u << 0, /**< Detach after the first matching event. */
    LND_NOTIFY_MANUAL = 1u << 1, /**< Disable worker dispatch; poll with SubscriptionRead or dispatch a supplied callback through LibraryUpdate. */
};

/** Copied event measured by rendering progress, not device presentation time. Source positions and
 * node render clocks can diverge after seeking, resampling or stretching. Parameter/value fields
 * apply to parameter events.
 */
typedef struct LND_NOTIFICATION {
    int32_t type; /**< LND_NOTIFY event identifier. */
    int32_t param; /**< Parameter ID for a parameter-specific event. */
    uint64_t position_frames; /**< Event position in frames at sample_rate_hz. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    float value; /**< Parameter value in its defined units. */
} LND_NOTIFICATION;

/** Receive a notification borrowed for this call with user; automatic delivery may use a library
 * thread.
 *
 * @param user Borrowed callback context.
 * @param notification Notification borrowed for this call.
 */
typedef void (*LND_NOTIFICATION_PROC)(void *user, const LND_NOTIFICATION *notification);

/** Event filter, queue and dispatch settings copied at subscription; callback user remains borrowed. */
typedef struct LND_SUBSCRIPTION_CONFIG {
    int32_t type; /**< LND_NOTIFY event to subscribe to. */
    int32_t param; /**< Parameter ID for a parameter-specific event. */
    uint64_t position_frames; /**< Target position for a POSITION notification, in frames. */
    uint32_t notification_capacity; /**< Event queue capacity; zero selects the default. */
    uint32_t flags; /**< LND_NOTIFY_ONCE and MANUAL bits. */
    LND_NOTIFICATION_PROC proc; /**< Optional delivery callback; NULL selects manual queue reading. */
    void *user; /**< Borrowed context passed to callbacks. */
} LND_SUBSCRIPTION_CONFIG;

/** Create a subscription to sound using copied config.
 *
 * @param sound Sound to operate on.
 * @param config Required settings, borrowed during the call.
 * @return Owned subscription or NULL; release with LND_SubscriptionFree.
 */
LND_API LND_SUBSCRIPTION *LND_SoundSubscribe(LND_SOUND *sound, const LND_SUBSCRIPTION_CONFIG *config);

/** Create a subscription to node using copied config.
 *
 * @param node Graph node to operate on.
 * @param config Required settings, borrowed during the call.
 * @return Owned subscription or NULL; release with LND_SubscriptionFree.
 */
LND_API LND_SUBSCRIPTION *LND_NodeSubscribe(LND_NODE *node, const LND_SUBSCRIPTION_CONFIG *config);

/** Pop one manual notification into notification.
 *
 * @param subscription Notification subscription.
 * @param notification Receives the next notification.
 * @return 1 if read, 0 if empty, or a negative error; automatic subscriptions cannot be polled.
 */
LND_API int32_t LND_SubscriptionRead(LND_SUBSCRIPTION *subscription, LND_NOTIFICATION *notification);

/** Get notifications lost because subscription's queue was full.
 *
 * @param subscription Notification subscription.
 * @return Notifications lost because subscription's queue was full.
 */
LND_API uint64_t LND_SubscriptionGetDroppedCount(const LND_SUBSCRIPTION *subscription);

/** Check whether subscription is still attached to a live owner.
 *
 * @param subscription Notification subscription.
 * @return True if subscription is still attached to a live owner; false otherwise.
 */
LND_API bool LND_SubscriptionIsAttached(const LND_SUBSCRIPTION *subscription);

/** Check whether subscription dispatches its callback automatically.
 *
 * @param subscription Notification subscription.
 * @return True if subscription dispatches its callback automatically; false otherwise.
 */
LND_API bool LND_SubscriptionIsAutomatic(const LND_SUBSCRIPTION *subscription);

/** Detach and release subscription, including its queued events.
 *
 * @param subscription Notification subscription.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SubscriptionFree(LND_SUBSCRIPTION *subscription);

#ifdef __cplusplus
}
#endif
