#pragma once

#include "lindar.h"

int32_t lnd_modules_init(void);
int32_t lnd_modules_start(void);
void lnd_modules_stop(void);
void lnd_modules_free(void);
void lnd_modules_update(void);
void lnd_modules_maintain(void);

const char *lnd_modules_error_string(int32_t code);
