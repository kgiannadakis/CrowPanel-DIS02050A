// MeshCore-style portrait UI for CrowPanel DIS05020A v1.1
// Phase 1: foundation, tab bar, placeholder screens, keyboard widget
#pragma once

#if HAS_TFT && USE_MCUI

namespace mcui {

// Logical screen dimensions depend on the persisted mcui orientation.
constexpr int PORTRAIT_SCR_W = 480;
constexpr int PORTRAIT_SCR_H = 800;
constexpr int LANDSCAPE_SCR_W = 800;
constexpr int LANDSCAPE_SCR_H = 480;
constexpr int STATUS_H = 40;
constexpr int TAB_H = 60;
constexpr int PAGE_Y = STATUS_H;

int screen_width();
int screen_height();
int page_height();
int keyboard_height();

bool landscape_active();
bool orientation_save(bool landscape);
bool position_advert_enabled();
bool position_advert_save(bool enabled);

enum Tab {
    TAB_CHATS = 0,
    TAB_NODES = 1,
    TAB_MAPS = 2,
    TAB_SETTINGS = 3,
};

// Entry point: called from tftSetup()
void setup();

// Switch active tab (exposed for tab bar callbacks)
void switchTab(int idx);

} // namespace mcui

#define SCR_W (::mcui::screen_width())
#define SCR_H (::mcui::screen_height())
#define PAGE_H (::mcui::page_height())

#endif // HAS_TFT && USE_MCUI
