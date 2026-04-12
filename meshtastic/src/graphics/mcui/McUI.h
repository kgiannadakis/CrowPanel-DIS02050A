// MeshCore-style portrait UI for CrowPanel DIS05020A v1.1
// Phase 1: foundation, tab bar, placeholder screens, keyboard widget
#pragma once

#if HAS_TFT && USE_MCUI

namespace mcui {

// Logical portrait dimensions
constexpr int SCR_W = 480;
constexpr int SCR_H = 800;
constexpr int STATUS_H = 40;
constexpr int TAB_H = 60;
constexpr int PAGE_Y = STATUS_H;
constexpr int PAGE_H = SCR_H - STATUS_H - TAB_H;

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

#endif // HAS_TFT && USE_MCUI
