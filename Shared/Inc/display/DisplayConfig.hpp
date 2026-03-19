#pragma once

// ═══════════════════════════════════════════════════
//  Display Abstraction Layer — Feature Flags
//
//  0 = code exists but NOT compiled (zero cost)
//  1 = compiled into binary
//
//  Only enable what you need — saves Flash + RAM.
// ═══════════════════════════════════════════════════

// ── Core (always on) ─────────────────────────────
#define DISPLAY_FEATURE_CORE        1   // IDisplay, Color, colors namespace, g_display

// ── Decorators / Utilities ───────────────────────
#define DISPLAY_FEATURE_MUTEX       1   // MutexDisplay — thread-safe decorator
#define DISPLAY_FEATURE_STATUS      1   // statusLine() / headerBar() / clearStatusLine()

// ── Input Types ──────────────────────────────────
#define DISPLAY_FEATURE_TOUCH       1   // TouchEvent, Gesture, TouchPoint
#define DISPLAY_FEATURE_KEY_NAV     1   // KeyEvent (physical buttons KEY1/KEY2)

// ── Widget System ────────────────────────────────
#define DISPLAY_FEATURE_WIDGETS     1   // Widget base class + WidgetGroup (focus)
#define DISPLAY_FEATURE_BUTTON      1   // BitmapButton (XBM icon, tap/longPress)
#define DISPLAY_FEATURE_FORM        1   // Label, Checkbox, RadioGroup, Slider,
                                        // ProgressBar, NumberStepper

// ── Dialogs / Overlays ───────────────────────────
#define DISPLAY_FEATURE_DIALOGS     1   // AlertDialog, ConfirmDialog, Toast

// ── Navigation ───────────────────────────────────
#define DISPLAY_FEATURE_NAV_STACK   1   // ViewManager push/pop multi-level navigation
