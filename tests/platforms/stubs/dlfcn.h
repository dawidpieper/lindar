#pragma once

#define RTLD_DEFAULT ((void *)0)
#define dlsym lnd_test_dlsym
void *dlsym(void *handle, const char *symbol);
