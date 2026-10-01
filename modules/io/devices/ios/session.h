#pragma once

#include "lindar_ios.h"

int32_t lnd_ios_session_acquire(bool capture);
void lnd_ios_session_release(bool capture);
uint32_t lnd_ios_session_input_channels(void);
void lnd_ios_free(void);

int32_t lnd_ios_native_configure(const LND_IOS_SESSION_CONFIG *config);
int32_t lnd_ios_native_active(bool active);
int32_t lnd_ios_native_input(int32_t recording, int32_t orientation);
int32_t lnd_ios_native_orientation(int32_t orientation);
