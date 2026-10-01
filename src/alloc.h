#pragma once

#include "platform.h"

void *lnd_alloc(size_t size);
void *lnd_alloc_zero(size_t size);
void *lnd_realloc(void *ptr, size_t size);
void lnd_free(void *ptr);
void *lnd_alloc_aligned(size_t size, size_t align);
void lnd_free_aligned(void *ptr);
char *lnd_strdup(const char *s);
