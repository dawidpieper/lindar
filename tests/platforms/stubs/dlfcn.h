#pragma once

#define RTLD_DEFAULT ((void *)0)
void *dlsym(void *handle, const char *symbol);
