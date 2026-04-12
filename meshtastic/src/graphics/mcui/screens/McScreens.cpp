#if HAS_TFT && USE_MCUI

#include "McScreens.h"
#include "McChatView.h"
#include "../McTheme.h"
#include "../McUI.h"
#include "../data/McMessages.h"

#include "configuration.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/channel.pb.h"

#include <cstdio>
#include <cstring>

namespace mcui {

// ---- Shared helpers --------------------------------------------------------

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, SCR_W, PAGE_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *add_placeholder(lv_obj_t *page, const char *title, const char *hint)
{
    lv_obj_t *t = lv_label_create(page);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(TH_TEXT), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *h = lv_label_create(page);
    lv_label_set_text(h, hint);
    lv_obj_set_style_text_color(h, lv_color_hex(TH_TEXT3), 0);
    lv_obj_set_width(h, SCR_W - 40);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 0);
    return page;
}

// ============================================================================
//  Chats screen
// ============================================================================

// Static state for the chats screen ------------------------------------------
static lv_obj_t *s_chats_page = nullptr;  // the "page" container we own
static lv_obj_t *s_chats_list = nullptr;  // scrollable list container
static lv_obj_t *s_chat_delete_overlay = nullptr;
static uint32_t s_chats_last_tick = 0xFFFFFFFFu;
static McConvId s_pending_delete_id = McConvId::none();
static McConvId s_suppress_click_id = McConvId::none();
static uint32_t s_suppress_click_ms = 0;

// Backing array of McConvIds, one per list card. Rebuilt on every refresh,
// so each card's user_data pointer remains valid for its lifetime.
struct ChatEntry {
    McConvId id;
    char title[40];
};
static ChatEntry s_entries[MC_MAX_CONVERSATIONS + 8];
static int s_num_entries = 0;

static void rebuild_chats_list();

// Called when a conversation card is tapped.
static void chat_card_clicked(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    ChatEntry *ent = (ChatEntry *)lv_obj_get_user_data(obj);
    if (!ent) return;
    if (s_suppress_click_id.is_valid() && ent->id == s_suppress_click_id) {
        if ((uint32_t)(lv_tick_get() - s_suppress_click_ms) < 1200) {
            s_suppress_click_id = McConvId::none();
            return;
        }
        s_suppress_click_id = McConvId::none();
    }
    chatview_open(ent->id, ent->title);
}

static void chat_delete_overlay_close()
{
    if (s_chat_delete_overlay) {
        lv_obj_delete(s_chat_delete_overlay);
        s_chat_delete_overlay = nullptr;
    }
    s_pending_delete_id = McConvId::none();
}

static void chat_delete_confirm_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    bool do_delete = (btn == (lv_obj_t *)lv_event_get_user_data(e));
    McConvId id = s_pending_delete_id;
    chat_delete_overlay_close();

    if (do_delete && id.kind == McConvId::DIRECT) {
        messages_delete_conv(id);
        rebuild_chats_list();
    }
}

static void chat_card_long_pressed(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    ChatEntry *ent = (ChatEntry *)lv_obj_get_user_data(obj);
    if (!ent || ent->id.kind != McConvId::DIRECT) return;
    s_suppress_click_id = ent->id;
    s_suppress_click_ms = lv_tick_get();

    lv_indev_t *indev = lv_indev_active();
    if (indev)
        lv_indev_wait_release(indev);

    chat_delete_overlay_close();
    s_pending_delete_id = ent->id;

    lv_obj_t *scr = lv_screen_active();
    s_chat_delete_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_chat_delete_overlay);
    lv_obj_set_size(s_chat_delete_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(s_chat_delete_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_chat_delete_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_chat_delete_overlay, LV_OPA_60, 0);
    lv_obj_remove_flag(s_chat_delete_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_chat_delete_overlay);

    lv_obj_t *card = lv_obj_create(s_chat_delete_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, SCR_W - 48, 250);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(TH_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    char title[88];
    snprintf(title, sizeof(title), "Delete private chat?\n%s", ent->title);
    lv_obj_t *tl = lv_label_create(card);
    lv_label_set_text(tl, title);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(tl, lv_pct(100));
    lv_obj_set_style_text_color(tl, lv_color_hex(TH_TEXT), 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *body = lv_label_create(card);
    lv_label_set_text(body, "This deletes only the message history. The node stays in your node list.");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_color(body, lv_color_hex(TH_TEXT2), 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 64);

    lv_obj_t *cancel = lv_button_create(card);
    lv_obj_set_size(cancel, 150, 42);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(TH_INPUT), 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_hex(TH_TEXT), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_16, 0);
    lv_obj_center(cl);

    lv_obj_t *delete_btn = lv_button_create(card);
    lv_obj_set_size(delete_btn, 150, 42);
    lv_obj_align(delete_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0xB83232), 0);
    lv_obj_set_style_radius(delete_btn, 8, 0);
    lv_obj_t *dl = lv_label_create(delete_btn);
    lv_label_set_text(dl, "Delete chat");
    lv_obj_set_style_text_color(dl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
    lv_obj_center(dl);

    lv_obj_add_event_cb(cancel, chat_delete_confirm_cb, LV_EVENT_CLICKED, delete_btn);
    lv_obj_add_event_cb(delete_btn, chat_delete_confirm_cb, LV_EVENT_CLICKED, delete_btn);
}

// Add a section header label ("Channels" / "Direct")
static void add_section_header(const char *text)
{
    lv_obj_t *h = lv_label_create(s_chats_list);
    lv_label_set_text(h, text);
    lv_obj_set_style_text_color(h, lv_color_hex(TH_TEXT3), 0);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(h, 6, 0);
    lv_obj_set_style_pad_left(h, 12, 0);
}

// Add one conversation card (channel or direct).
static void add_chat_card(ChatEntry *ent, const char *subtitle, uint16_t unread,
                          uint32_t accent_color)
{
    lv_obj_t *card = lv_obj_create(s_chats_list);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, 64);
    lv_obj_set_style_bg_color(card, lv_color_hex(TH_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(card, ent);
    lv_obj_add_event_cb(card, chat_card_clicked, LV_EVENT_CLICKED, nullptr);
    if (ent->id.kind == McConvId::DIRECT)
        lv_obj_add_event_cb(card, chat_card_long_pressed, LV_EVENT_LONG_PRESSED, nullptr);

    // Round accent dot/avatar (first initial)
    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 40, 40);
    lv_obj_set_pos(dot, 0, 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(accent_color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    char initial[6] = {0};
    if (ent->title[0]) {
        initial[0] = ent->title[0];
        initial[1] = '\0';
    } else {
        initial[0] = '?';
    }
    lv_obj_t *dotl = lv_label_create(dot);
    lv_label_set_text(dotl, initial);
    lv_obj_set_style_text_color(dotl, lv_color_hex(TH_TEXT), 0);
    lv_obj_center(dotl);

    // Title
    lv_obj_t *tl = lv_label_create(card);
    lv_label_set_text(tl, ent->title);
    lv_obj_set_style_text_color(tl, lv_color_hex(TH_TEXT), 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(tl, 52, 2);
    lv_obj_set_width(tl, SCR_W - 52 - 20 - 40);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);

    // Subtitle / preview
    if (subtitle && subtitle[0]) {
        lv_obj_t *sl = lv_label_create(card);
        lv_label_set_text(sl, subtitle);
        lv_obj_set_style_text_color(sl, lv_color_hex(TH_TEXT2), 0);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(sl, 52, 24);
        lv_obj_set_width(sl, SCR_W - 52 - 20 - 40);
        lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    }

    // Unread badge
    if (unread > 0) {
        lv_obj_t *badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 26, 22);
        lv_obj_set_style_radius(badge, 11, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(TH_ACCENT), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        char num[6];
        snprintf(num, sizeof(num), "%u", (unsigned)(unread > 99 ? 99 : unread));
        lv_obj_t *bl = lv_label_create(badge);
        lv_label_set_text(bl, num);
        lv_obj_set_style_text_color(bl, lv_color_hex(TH_TEXT), 0);
        lv_obj_center(bl);
    }
}

// Pull a short preview string for a conversation into `out`.
static void fill_preview(const McConvId &id, char *out, size_t out_sz)
{
    McMessage last;
    if (!messages_last(id, last)) {
        snprintf(out, out_sz, "No messages yet");
        return;
    }
    const char *prefix = last.outgoing ? "You: " : "";
    snprintf(out, out_sz, "%s%s", prefix, last.text);
}

struct ConvGather {
    McConvId ids[MC_MAX_CONVERSATIONS];
    int n;
};
static void conv_gather_cb(const McConvId &id, void *ctx)
{
    ConvGather *g = (ConvGather *)ctx;
    if (g->n < (int)(sizeof(g->ids) / sizeof(g->ids[0]))) {
        g->ids[g->n++] = id;
    }
}

static void rebuild_chats_list()
{
    if (!s_chats_list) return;
    lv_obj_clean(s_chats_list);
    s_num_entries = 0;

    // ---- Section 1: Channels (pinned) --------------------------------------
    add_section_header("Channels");

    uint8_t nch = channels.getNumChannels();
    for (uint8_t i = 0; i < nch && s_num_entries < (int)(sizeof(s_entries)/sizeof(s_entries[0])); i++) {
        meshtastic_Channel &ch = channels.getByIndex(i);
        if (ch.role == meshtastic_Channel_Role_DISABLED) continue;

        ChatEntry *ent = &s_entries[s_num_entries++];
        ent->id = McConvId::channel(i);
        const char *name = channels.getName(i);
        if (!name || !name[0]) name = (i == 0) ? "Primary" : "Channel";
        strncpy(ent->title, name, sizeof(ent->title) - 1);
        ent->title[sizeof(ent->title) - 1] = '\0';

        char preview[80];
        fill_preview(ent->id, preview, sizeof(preview));
        uint16_t u = messages_unread(ent->id);
        add_chat_card(ent, preview, u, TH_ACCENT);
    }

    // ---- Section 2: Direct conversations -----------------------------------
    ConvGather g;
    g.n = 0;
    messages_for_each_conv(conv_gather_cb, &g);

    bool any_direct = false;
    for (int i = 0; i < g.n; i++) {
        if (g.ids[i].kind != McConvId::DIRECT) continue;
        any_direct = true;
        break;
    }
    if (any_direct) {
        add_section_header("Direct");
    }

    for (int i = 0; i < g.n && s_num_entries < (int)(sizeof(s_entries)/sizeof(s_entries[0])); i++) {
        if (g.ids[i].kind != McConvId::DIRECT) continue;

        ChatEntry *ent = &s_entries[s_num_entries++];
        ent->id = g.ids[i];

        // Resolve display name from NodeDB
        const char *title = nullptr;
        char fallback[16];
        if (nodeDB) {
            auto *n = nodeDB->getMeshNode((NodeNum)g.ids[i].value);
            if (n && n->has_user) {
                if (n->user.long_name[0])
                    title = n->user.long_name;
                else if (n->user.short_name[0])
                    title = n->user.short_name;
            }
        }
        if (!title) {
            snprintf(fallback, sizeof(fallback), "!%08x", (unsigned)g.ids[i].value);
            title = fallback;
        }
        strncpy(ent->title, title, sizeof(ent->title) - 1);
        ent->title[sizeof(ent->title) - 1] = '\0';

        char preview[80];
        fill_preview(ent->id, preview, sizeof(preview));
        uint16_t u = messages_unread(ent->id);
        add_chat_card(ent, preview, u, TH_BUBBLE_OUT);
    }

    s_chats_last_tick = messages_change_tick();
}

lv_obj_t *chats_screen_create(lv_obj_t *parent)
{
    s_chats_page = make_page(parent);

    // Scrollable list container fills the whole page
    s_chats_list = lv_obj_create(s_chats_page);
    lv_obj_remove_style_all(s_chats_list);
    lv_obj_set_size(s_chats_list, SCR_W, PAGE_H);
    lv_obj_set_pos(s_chats_list, 0, 0);
    lv_obj_set_style_bg_color(s_chats_list, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(s_chats_list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_chats_list, 8, 0);
    lv_obj_set_style_pad_row(s_chats_list, 6, 0);
    lv_obj_set_flex_flow(s_chats_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_chats_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chats_list, LV_SCROLLBAR_MODE_AUTO);

    // Note: the bubble chat view is created by McUI at root-screen level so
    // it can cover the tab bar area when the keyboard is up.

    // Force first build
    s_chats_last_tick = 0xFFFFFFFFu;
    rebuild_chats_list();
    return s_chats_page;
}

void chats_screen_tick()
{
    // Let the chat view process its own refresh (bubble updates)
    chatview_tick();

    if (!s_chats_list) return;
    if (chatview_is_open()) return;
    uint32_t t = messages_change_tick();
    if (t != s_chats_last_tick) {
        rebuild_chats_list();
    }
}

// ============================================================================
//  Other (placeholder) screens
// ============================================================================

// nodes_screen_create() lives in McNodes.cpp

lv_obj_t *maps_screen_create(lv_obj_t *parent)
{
    lv_obj_t *p = make_page(parent);
    add_placeholder(p, "Maps", "Work in progress, mostly expected in the P4 release");
    return p;
}

// settings_screen_create() lives in McSettings.cpp

} // namespace mcui

#endif
