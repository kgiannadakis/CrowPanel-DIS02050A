#if HAS_TFT && USE_MCUI

#include "McSender.h"
#include "configuration.h"

#include "concurrency/OSThread.h"
#include "mesh/MeshService.h"
#include "mesh/MeshTypes.h"
#include "mesh/NextHopRouter.h"
#include "mesh/NodeDB.h"
#include "mesh/ReliableRouter.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

namespace mcui {

// The UI task runs on core 0. The mesh stack (router/service/LoRa) runs on
// the main Arduino loop on core 1. Calling sendToMesh() directly from the UI
// task would race with the mesh loop and crash. Instead we queue the request
// and drain it from a concurrency::OSThread which runs on the main loop.

struct PendingSend {
    McConvId id;
    size_t len;
    char text[228];
};

static constexpr int SEND_QUEUE_DEPTH = 4;
static PendingSend s_queue[SEND_QUEUE_DEPTH];
static int s_q_head = 0;
static int s_q_count = 0;
static SemaphoreHandle_t s_q_lock = nullptr;

// After a DM is handed to sendToMesh() we need to flip the chat bubble from
// "sent" to either "acknowledged" (real ACK landed) or "failed" (NAK landed
// or retries exhausted with no response). We use two complementary signals:
//
//  1. A direct observer hook in ReliableRouter::sniffReceived (the g_ackNakObserver
//     callback, see ReliableRouter.h). This is the ground truth — it fires
//     exactly when a Routing packet referencing our outgoing id arrives,
//     and tells us whether it was an ACK (error_reason == NONE) or a NAK
//     (any other error code). This is how the phone app tracks delivery.
//
//  2. A poll of NextHopRouter::pending for the retransmission entry's
//     presence. This is the fallback for the "retry exhausted with no
//     response at all" case (e.g. the peer is powered off). When the entry
//     disappears AND the observer never fired for it, we know the router
//     gave up and we mark the bubble as failed.
//
// We intentionally do NOT use Router::findInTxQueue() for any of this. That
// queries the radio interface's *outbound* txQueue, which empties as soon as
// the packet hits the air once — well before any ACK. It's useless for
// delivery tracking.
struct PendingAck {
    uint32_t packet_id;
    uint32_t from_node;   // our node num
    McConvId conv;
    uint32_t first_seen_ms;
    bool observed_pending; // true once we've seen it in the retransmission table
    bool finalized;        // true once the observer hook has resolved this entry
};
static constexpr int PENDING_ACK_MAX = 8;
static constexpr uint32_t PENDING_TIMEOUT_MS = 120000; // 2 minutes
static PendingAck s_pending[PENDING_ACK_MAX];

static bool enqueue(const PendingSend &ps)
{
    if (!s_q_lock) return false;
    bool ok = false;
    xSemaphoreTake(s_q_lock, portMAX_DELAY);
    if (s_q_count < SEND_QUEUE_DEPTH) {
        int idx = (s_q_head + s_q_count) % SEND_QUEUE_DEPTH;
        s_queue[idx] = ps;
        s_q_count++;
        ok = true;
    }
    xSemaphoreGive(s_q_lock);
    return ok;
}

static bool dequeue(PendingSend &out)
{
    if (!s_q_lock) return false;
    bool ok = false;
    xSemaphoreTake(s_q_lock, portMAX_DELAY);
    if (s_q_count > 0) {
        out = s_queue[s_q_head];
        s_q_head = (s_q_head + 1) % SEND_QUEUE_DEPTH;
        s_q_count--;
        ok = true;
    }
    xSemaphoreGive(s_q_lock);
    return ok;
}

// Record a new ACK-pending entry. Called from do_send on the main loop, so
// no locking needed (McSendThread::runOnce and the mesh rx path both run
// from the same main-loop context on core 1).
//
// If the table is already full we evict the oldest entry (by first_seen_ms)
// and mark its corresponding chat row as failed. This keeps new sends
// trackable and ensures no row stays stuck at "sent" indefinitely when
// under heavy traffic — the alternative (silently dropping tracking for
// the new send, which is what this function used to do) meant the newest
// message was the one that got orphaned.
static void pending_add(uint32_t packet_id, uint32_t from_node, const McConvId &conv)
{
    // Prefer a free slot
    for (int i = 0; i < PENDING_ACK_MAX; i++) {
        if (s_pending[i].packet_id == 0) {
            s_pending[i].packet_id = packet_id;
            s_pending[i].from_node = from_node;
            s_pending[i].conv = conv;
            s_pending[i].first_seen_ms = millis();
            s_pending[i].observed_pending = false;
            s_pending[i].finalized = false;
            return;
        }
    }

    // All slots taken — evict the oldest entry. Mark its row failed first
    // so the user sees it transition rather than just stall.
    int oldest = 0;
    for (int i = 1; i < PENDING_ACK_MAX; i++) {
        if (s_pending[i].first_seen_ms < s_pending[oldest].first_seen_ms) {
            oldest = i;
        }
    }
    LOG_WARN("mcui: pending-ack table full; evicting oldest id=%u to track id=%u",
             (unsigned)s_pending[oldest].packet_id, (unsigned)packet_id);
    messages_mark_ack_by_packet_id(s_pending[oldest].packet_id, false);

    s_pending[oldest].packet_id = packet_id;
    s_pending[oldest].from_node = from_node;
    s_pending[oldest].conv = conv;
    s_pending[oldest].first_seen_ms = millis();
    s_pending[oldest].observed_pending = false;
    s_pending[oldest].finalized = false;
}

// ReliableRouter observer hook. Called from the main loop when a Routing
// packet referencing one of our outgoing ids lands. This is the ground truth
// for delivery status: `isAck` distinguishes ACK (true) from NAK (false).
static void on_ack_nak_received(NodeNum from, PacketId id, bool isAck)
{
    // We only track DMs we sent ourselves. ReliableRouter calls us with
    // `from` == p->to == us for ACKs destined back to us, so we don't need
    // to filter by node here — the packet_id match is unique enough.
    (void)from;
    for (int i = 0; i < PENDING_ACK_MAX; i++) {
        PendingAck &e = s_pending[i];
        if (e.packet_id != id) continue;
        LOG_INFO("mcui: %s observed for id=%u", isAck ? "ACK" : "NAK", (unsigned)id);
        messages_mark_ack_by_packet_id(e.packet_id, isAck);
        e.finalized = true;
        // Don't zero the slot yet — let poll_pending_acks reap it on the
        // next tick so we don't race with its own iteration.
        return;
    }
    // Not ours — some other subsystem's packet. Silently ignore.
}

// Perform the actual send. Called from the main loop (OSThread) only.
static void do_send(const PendingSend &ps)
{
    if (!router || !service) {
        LOG_WARN("mcui: send dropped — router/service null");
        // Reconcile the optimistic bubble that sender_send_text appended.
        // Channel broadcasts were already marked delivered at append time,
        // so only DMs need reconciling here.
        if (ps.id.kind == McConvId::DIRECT) {
            messages_mark_last_unsent_as_failed(ps.id);
        }
        return;
    }

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("mcui: send dropped — allocForSending returned null");
        if (ps.id.kind == McConvId::DIRECT) {
            messages_mark_last_unsent_as_failed(ps.id);
        }
        return;
    }

    if (ps.id.kind == McConvId::DIRECT) {
        p->to = ps.id.value;
        p->want_ack = true;
        p->channel = 0;
    } else {
        p->to = NODENUM_BROADCAST;
        p->want_ack = false;
        p->channel = (uint8_t)ps.id.value;
    }
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.payload.size = ps.len;
    memcpy(p->decoded.payload.bytes, ps.text, ps.len);

    // Capture identity BEFORE sendToMesh — the packet pool reclaims p after.
    const uint32_t sent_id = p->id;
    const uint32_t sent_from = p->from ? p->from : (nodeDB ? nodeDB->getNodeNum() : 0);

    service->sendToMesh(p, RX_SRC_LOCAL, true);

    // Patch the mirror row in the message store with the real packet id so
    // we can later flip its delivered flag when the ACK lands.
    messages_attach_packet_id(ps.id, sent_id);

    // Only DMs carry ACKs (reliable router). Channel broadcasts have no ACK
    // mechanism — those rows were already marked delivered by sender_send_text.
    if (ps.id.kind == McConvId::DIRECT) {
        pending_add(sent_id, sent_from, ps.id);
    }

    LOG_INFO("mcui: sent %u bytes to %s %u (id=%u)",
             (unsigned)ps.len,
             ps.id.kind == McConvId::DIRECT ? "node" : "ch",
             (unsigned)ps.id.value,
             (unsigned)sent_id);
}

// Reap finalized entries and drive the retry-exhaustion fallback.
//
// The real-ACK path is handled by on_ack_nak_received above: that fires
// synchronously from ReliableRouter::sniffReceived when a matching routing
// packet lands, and marks the entry `finalized`. This function does two
// things:
//
//   1. Reap any entry whose `finalized` flag is set (cheap, next tick).
//   2. For entries that are NOT finalized, watch NextHopRouter::pending.
//      While the retry table still holds our packet we note observed_pending
//      and keep waiting. If the entry disappears from the retry table and
//      we still haven't been finalized by the observer, that means the
//      router ran through NUM_RELIABLE_RETX retries without ever seeing an
//      ACK or NAK — treat it as a delivery failure.
//   3. Absolute safety timeout (PENDING_TIMEOUT_MS): if something upstream
//      broke and neither signal ever fired, flip to failed so the UI
//      doesn't stall forever.
static void poll_pending_acks()
{
    if (!router) return;
    // ReliableRouter -> NextHopRouter -> Router; findPendingPacket lives on
    // NextHopRouter. router is guaranteed to be a ReliableRouter (main.cpp).
    NextHopRouter *nhr = static_cast<NextHopRouter *>(router);

    uint32_t now = millis();
    for (int i = 0; i < PENDING_ACK_MAX; i++) {
        PendingAck &e = s_pending[i];
        if (e.packet_id == 0) continue;

        if (e.finalized) {
            // The observer already marked the row delivered/failed; just
            // recycle the slot.
            e.packet_id = 0;
            continue;
        }

        bool pending = nhr->isAwaitingAck(e.from_node, e.packet_id);
        if (pending) {
            e.observed_pending = true;
            // Still in the retry table — waiting for ACK. The observer will
            // finalize us when (if) one lands.
        } else if (e.observed_pending) {
            // Was in the retry table, now gone, and the observer never
            // fired. That means the router ran out of retries without ever
            // seeing an ACK/NAK — delivery failed. Typical case: peer is
            // powered off (~30-60s total).
            LOG_INFO("mcui: retries exhausted for id=%u -> failed",
                     (unsigned)e.packet_id);
            messages_mark_ack_by_packet_id(e.packet_id, false);
            e.packet_id = 0;
            continue;
        }
        // Absolute safety timeout: if we never even saw the packet enter
        // the retry table (e.g. sendToMesh returned an error we didn't
        // catch) flip the bubble so the UI doesn't stall.
        if ((now - e.first_seen_ms) >= PENDING_TIMEOUT_MS) {
            LOG_WARN("mcui: ack timeout id=%u -> failed", (unsigned)e.packet_id);
            messages_mark_ack_by_packet_id(e.packet_id, false);
            e.packet_id = 0;
        }
    }
}

class McSendThread : public concurrency::OSThread
{
  public:
    McSendThread() : concurrency::OSThread("McSender") {}

  protected:
    int32_t runOnce() override
    {
        PendingSend ps;
        while (dequeue(ps)) {
            do_send(ps);
        }
        // Drive the ACK tracker for outgoing DMs.
        poll_pending_acks();
        return 200; // poll every 200 ms
    }
};

static McSendThread *s_thread = nullptr;

void sender_init()
{
    if (!s_q_lock) s_q_lock = xSemaphoreCreateMutex();
    if (!s_thread) s_thread = new McSendThread();
    // Register our ACK/NAK observer with ReliableRouter so we get notified
    // the moment a routing packet addressed to us lands that references one
    // of our outgoing packet ids. See ReliableRouter.h for contract.
    // Idempotent: setting the same pointer twice is harmless.
    g_ackNakObserver = &on_ack_nak_received;
}

bool sender_send_text(const McConvId &id, const char *text)
{
    if (!text || !text[0]) return false;

    // Lazily construct the worker the first time send is called — this keeps
    // the thread off the main schedule until actually needed.
    sender_init();
    if (!s_q_lock) return false;

    size_t len = strlen(text);
    if (len > sizeof(((PendingSend *)nullptr)->text)) {
        len = sizeof(((PendingSend *)nullptr)->text);
    }

    PendingSend ps = {};
    ps.id = id;
    ps.len = len;
    memcpy(ps.text, text, len);
    if (!enqueue(ps)) {
        LOG_WARN("mcui: send queue full, dropping message");
        return false;
    }

    // Mirror into the local store so the bubble shows up immediately.
    // messages_* is thread-safe via its own mutex.
    //
    // Delivery semantics:
    //  - Channel broadcasts have no ACK in the protocol, so we mark them
    //    delivered immediately.
    //  - DMs start as NOT delivered; the McSendThread polls the router's
    //    TX queue and flips the flag once the ReliableRouter removes the
    //    packet (meaning ACK received or retries exhausted).
    //
    // The packet_id is 0 initially and is patched to the real value by
    // do_send() once allocForSending() assigns one.
    McMessage m = {};
    m.from_node = 0;
    m.timestamp = (uint32_t)time(nullptr);
    m.outgoing = true;
    m.delivered = (id.kind != McConvId::DIRECT);
    m.packet_id = 0;
    strncpy(m.text, text, sizeof(m.text) - 1);
    messages_append(id, m);

    return true;
}

} // namespace mcui

#endif
