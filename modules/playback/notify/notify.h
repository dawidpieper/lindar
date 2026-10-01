#pragma once

#include "src/notify.h"

void lnd_notify_dispatch(bool automatic);
int32_t lnd_notify_dispatch_start(void);
void lnd_notify_dispatch_wake(void);
void lnd_notify_dispatch_stop(void);
