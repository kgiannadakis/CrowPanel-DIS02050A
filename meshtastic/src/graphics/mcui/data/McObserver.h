// Subscribes to incoming text messages and funnels them into the mcui store.
#pragma once
#if HAS_TFT && USE_MCUI

namespace mcui {
// Register the observer. Safe to call multiple times; only registers once.
// Must be called after textMessageModule is constructed (i.e. after
// setupModules() in main.cpp, which already happens before tftSetup()).
void observer_init();
} // namespace mcui

#endif
