#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef const char *CFStringRef;
typedef long CFIndex;
typedef uint32_t CFStringEncoding;
enum { kCFStringEncodingUTF8 = 1 };
CFIndex CFStringGetLength(CFStringRef);
CFIndex CFStringGetMaximumSizeForEncoding(CFIndex, CFStringEncoding);
bool CFStringGetCString(CFStringRef, char *, CFIndex, CFStringEncoding);
void CFRelease(const void *);
