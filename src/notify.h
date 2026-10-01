#pragma once

#include "lnd_modules.h"

#if LND_MODULE_NOTIFY
#include "lindar_notify.h"

void lnd_notify_emit(LND_SUBSCRIPTION *head, int32_t type, uint64_t position, uint32_t rate, int32_t param, float value);
void lnd_notify_transport(LND_SUBSCRIPTION *head, int32_t type, uint64_t source_position, uint64_t node_position, uint32_t rate);
void lnd_notify_range(LND_SUBSCRIPTION *head, uint64_t start, uint64_t end, uint32_t rate, bool source_position);
void lnd_notify_detach(LND_SUBSCRIPTION **head);
#if LND_MODULE_GRAPH
void lnd_notify_node(LND_NODE *node, uint32_t frames);
#endif
#else
/* Disabled modules must not evaluate expressions using module-only fields. */
#define lnd_notify_emit(...) ((void)0)
#define lnd_notify_transport(...) ((void)0)
#define lnd_notify_range(...) ((void)0)
#define lnd_notify_detach(...) ((void)0)
#define lnd_notify_node(...) ((void)0)
#endif
