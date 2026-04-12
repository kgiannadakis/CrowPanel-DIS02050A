// Send text over the mesh from the mcui UI.
#pragma once
#if HAS_TFT && USE_MCUI

#include "McMessages.h"
#include <cstdint>

namespace mcui {

// Send a text string to a conversation. Depending on `id.kind`:
//   CHANNEL: broadcasts on channel `id.value`
//   DIRECT:  sends a direct message to node `id.value` with want_ack=true
//
// On success the outgoing message is appended to the local store so the
// bubble shows up immediately. Returns false if the packet couldn't be
// allocated or `text` is empty.
bool sender_send_text(const McConvId &id, const char *text);

} // namespace mcui

#endif
