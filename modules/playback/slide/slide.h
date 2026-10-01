#pragma once

#include "playback/graph/node.h"

#if LND_MODULE_SLIDE
uint32_t lnd_slide_before(lnd_node *node, uint32_t frames);
void lnd_slide_after(lnd_node *node, uint32_t frames);
bool lnd_slide_gain(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames);
void lnd_slide_cancel(lnd_node *node, int32_t param);
void lnd_slide_free(lnd_node *node);
#endif
