// Bubble chat view — a full-page overlay inside the Chats tab.
#pragma once
#if HAS_TFT && USE_MCUI

#include "../data/McMessages.h"
#include <lvgl.h>

namespace mcui {

// Create the chat view container inside `parent`. Starts hidden.
lv_obj_t *chatview_create(lv_obj_t *parent);

// Show the chat view for a conversation. Populates bubbles, sets title,
// loads keyboard etc. Call chatview_hide() to return to the list.
void chatview_open(const McConvId &id, const char *title);

// Hide the chat view (return to chat list).
void chatview_hide();

// Is the chat view currently visible?
bool chatview_is_open();

// Call this every UI tick; refreshes bubbles if the store has new data.
void chatview_tick();

// Currently displayed conversation (INVALID if none).
McConvId chatview_current();

} // namespace mcui

#endif
