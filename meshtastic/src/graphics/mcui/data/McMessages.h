// In-RAM message store for the mcui Chats screen.
//
// Phase 2 keeps everything in RAM. A "conversation" is identified by a
// McConvId, which is either a channel index (for channel chats) or a remote
// node number (for direct chats). Each conversation holds a small ring buffer
// of messages. The store is thread-safe via a FreeRTOS mutex; UI and mesh
// threads may both push/read.
#pragma once
#if HAS_TFT && USE_MCUI

#include <cstdint>
#include <cstddef>

namespace mcui {

// Conversation identifier. We pack "channel vs direct" into the high byte.
struct McConvId {
    enum Kind : uint8_t { CHANNEL = 0, DIRECT = 1, INVALID = 0xFF };
    Kind kind;
    // channel index (0..7) or direct node number
    uint32_t value;

    bool operator==(const McConvId &o) const { return kind == o.kind && value == o.value; }
    bool is_valid() const { return kind != INVALID; }
    static McConvId none() { return {INVALID, 0}; }
    static McConvId channel(uint8_t ch) { return {CHANNEL, ch}; }
    static McConvId direct(uint32_t node) { return {DIRECT, node}; }
};

// One stored message
struct McMessage {
    uint32_t from_node;   // sender nodeNum; 0 if we sent it
    uint32_t timestamp;   // unix seconds
    float    snr;         // 0 if outgoing
    int16_t  rssi;        // 0 if unknown
    bool     outgoing;    // true = we sent it
    bool     delivered;   // Final state reached (outgoing only). For DMs
                          // this flips once we've received an ACK, a NAK,
                          // or exhausted retries. For channel broadcasts it
                          // is set immediately on send.
    bool     ack_failed;  // Only meaningful when delivered==true. True if
                          // the final state was a NAK or retry exhaustion
                          // (so the chat bubble should show "failed"
                          // instead of "acknowledged"). Channel broadcasts
                          // leave this false.
    uint32_t packet_id;   // meshtastic PacketId (outgoing only, 0 until send
                          // side has called allocForSending; used by the
                          // sender worker to match ACKs back to this row)
    char     text[220];   // max text payload (shrunk by 4 more to absorb
                          // the new ack_failed bool + alignment padding so
                          // sizeof(McMessage) stays round)
};

// Max conversations tracked simultaneously (channels 0..7 + up to ~16 direct)
constexpr int MC_MAX_CONVERSATIONS = 24;
// Per-conversation ring buffer size
constexpr int MC_MAX_MSGS_PER_CONV = 32;

// ---- Public API ------------------------------------------------------------

// Initialize store (call once at UI startup).
void messages_init();

// Append a message to the conversation identified by id. Oldest entries are
// dropped when the ring is full. Safe to call from any task.
void messages_append(const McConvId &id, const McMessage &msg);

// Attach a packet_id to the most-recently-appended outgoing message in this
// conversation that still has packet_id == 0. Used by the sender worker after
// allocForSending() gives it a real id (we mirror into the store on the UI
// task before the id is known). No-op if no match found.
void messages_attach_packet_id(const McConvId &id, uint32_t packet_id);

// Mark the (first found) outgoing message with this packet_id as delivered
// and bump the change_tick so the chat view refreshes. No-op if no match.
void messages_mark_delivered_by_packet_id(uint32_t packet_id);

// Mark the (first found) outgoing message with this packet_id as in its
// final state, where `success` distinguishes real ACK (true) from
// NAK/retry-exhaustion (false). When success==true the bubble will show
// "acknowledged"; when false, "failed". No-op if no match.
void messages_mark_ack_by_packet_id(uint32_t packet_id, bool success);

// Mark the most-recent outgoing message in this conversation that still has
// packet_id == 0 (i.e. was optimistically appended by the UI but never made
// it as far as allocForSending) as failed. Used by the sender worker's
// early-return paths so a send-dispatch failure doesn't leave the bubble
// stuck at "sent" forever. Walks newest -> oldest to match the direction
// messages_attach_packet_id uses, so they stay consistent.
void messages_mark_last_unsent_as_failed(const McConvId &id);

// Copy the last `max_count` messages of a conversation into `out`, newest last.
// Returns the number of messages actually copied. `out` must be
// `max_count` entries long. Safe to call from any task.
size_t messages_snapshot(const McConvId &id, McMessage *out, size_t max_count);

// Get the last message in a conversation (for list previews). Returns false
// if conversation has no messages.
bool messages_last(const McConvId &id, McMessage &out);

// Count unread messages in a conversation.
uint16_t messages_unread(const McConvId &id);

// Mark a conversation as fully read.
void messages_mark_read(const McConvId &id);

// Delete a conversation and its stored messages. Returns true if a matching
// conversation existed.
bool messages_delete_conv(const McConvId &id);

// Iterate all conversations that have at least one message. The callback is
// invoked synchronously under the store lock; keep it fast. Used by the chats
// screen to build the "recent direct chats" list.
typedef void (*McConvVisitor)(const McConvId &id, void *ctx);
void messages_for_each_conv(McConvVisitor visit, void *ctx);

// A monotonically increasing counter, bumped whenever any conversation gets
// a new message. The chats screen polls this to decide when to rebuild the
// list. Cheap; no lock needed.
uint32_t messages_change_tick();

// Persistence — the chat store is mirrored to flash so it survives reboots.
// messages_load() is called from messages_init() automatically. messages_save()
// is called in a throttled way from the UI loop (every few seconds when the
// store is dirty). Both grab spiLock internally.
void messages_load();
void messages_save();
// Called each UI tick; if the store is dirty and enough time has passed, it
// flushes to flash. Cheap no-op otherwise.
void messages_save_tick();

// Tiny per-node last-heard RSSI cache. The text observer calls _set() whenever
// it sees a packet with a valid rx_rssi, and the Nodes screen calls _get() to
// display it. Returns 0 if no RSSI has been recorded for that node.
void node_rssi_set(uint32_t node_num, int16_t rssi);
int16_t node_rssi_get(uint32_t node_num);

} // namespace mcui

#endif
