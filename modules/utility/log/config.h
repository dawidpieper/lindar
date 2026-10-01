#pragma once

#include "lindar_log.h"

extern LND_CONFIG_KEY *const LND_CFG_LOG_PROC;
extern LND_CONFIG_KEY *const LND_CFG_LOG_USER;

#ifndef LND_DEFAULT_LOG_LEVEL
#define LND_DEFAULT_LOG_LEVEL LND_LOG_WARN
#endif
