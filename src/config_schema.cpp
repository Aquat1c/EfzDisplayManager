#include "efz_config_api.h"

#define SW(key, fallback, text) \
    { "Display", key, nullptr, text, EFZ_SETTING_SWITCH, 0, 0, nullptr, fallback }
#define NUM(key, lo, hi, fallback, text) \
    { "Display", key, nullptr, text, EFZ_SETTING_NUMBER, lo, hi, nullptr, fallback }
#define PICK(key, choices, fallback, text) \
    { "Display", key, nullptr, text, EFZ_SETTING_CHOICE, 0, 0, choices, fallback }

namespace {
const EfzConfigSetting kSettings[] = {
    SW("Enabled", "1", "1 = mod on, 0 = mod completely off."),

    PICK("Mode", "FitToScreen|IntegerScaling|CustomResolution", "FitToScreen",
         "How the 640x480 picture fills the window or screen:\n"
         "FitToScreen - as big as fits, black bars where needed\n"
         "IntegerScaling - whole multiples only (2x, 3x, ...): pixel-perfect, wider bars\n"
         "CustomResolution - a fixed size, set below"),
    NUM("IntegerScale", 0, 0, "0", "IntegerScaling only: the multiple to use. 0 = the largest that fits."),
    NUM("CustomWidth", 1, 16384, "640", "CustomResolution only: the picture width in pixels."),
    NUM("CustomHeight", 1, 16384, "480", "CustomResolution only: the picture height in pixels."),
    PICK("Filter", "Linear|Point|Sharp|xBR", "Linear",
         "Scaling filter:\n"
         "Linear - smooth (Revival's own look)\n"
         "Point - hard, crisp pixels\n"
         "Sharp - crisp but even; a good choice for big screens\n"
         "xBR - smoothed pixel art; heavy for laptop graphics above 1080p\n"
         "If your graphics card can't run Sharp or xBR, Linear is used."),
    NUM("Sharpness", 10, 40, "20", "Sharp only: how crisp, from 10 (soft) to 40 (hard)."),
    { "Display", "BackgroundColor", nullptr, "Color of the black bars, as RRGGBB (000000 = black).",
      EFZ_SETTING_TEXT, 0, 0, nullptr, "000000" },

    SW("FullscreenHotkey", "1", "F11 / Alt+Enter switch fullscreen on and off. 0 = keys do nothing."),
    SW("Fullscreen", "0", "1 = start the game in fullscreen."),
    PICK("FullscreenMode", "Exclusive|Borderless", "Exclusive",
         "Exclusive - the game takes over the screen.\n"
         "Borderless - a window covering the screen, for easier app switching.\n"
         "Exclusive may be unavailable on a secondary monitor."),
    NUM("FullscreenRefresh", 0, 0, "0",
        "Exclusive only: refresh rate in Hz. 0 = the screen's current rate.\n"
        "With VSync=Auto, 60 Hz is allowed and tearing is expected. VSync=1 requires\n"
        "at least 64 Hz; otherwise Borderless is used."),
    PICK("VSync", "Auto|0|1", "Auto",
         "Exclusive only: Auto (recommended) allows tearing so a 60 Hz screen does\n"
         "not set the game's pace. 0 does the same. 1 waits for screen refreshes,\n"
         "requires at least 64 Hz and can add delay. Driver settings also matter."),
    SW("HideCursor", "1",
       "Hide the mouse pointer in fullscreen after 2 seconds without movement.\n"
       "Moving the mouse shows it again. 0 = always show it."),
    SW("LowLatencyWindow", "0",
       "1 = try a newer windowed display mode that may reduce delay.\n"
       "Needs Windows 7 or newer and a screen of at least 64 Hz. Set 0 again if\n"
       "enabling it causes startup or overlay problems.\n"
       "With 1, OBS \"Window Capture\" shows a white or black picture unless its\n"
       "Capture Method is set to \"Windows 10 (1903 and up)\"."),

    SW("WindowAdjustStyle", "1", "Enable the window's maximize button."),
    SW("WindowDragResize", "1",
       "Let the window be resized by dragging its edges. Windows pauses the game\n"
       "while you hold an edge; maximizing and snapping never pause it."),
    SW("ResizeBackBuffer", "1", "The picture follows the window size. 0 = keep Revival's fixed size."),

    SW("CompatLayer", "1",
       "Works together with training mode 1.2.5+ and InGameNetplay 0.5.5+ when the\n"
       "display changes. Leave at 1. With 0, window resizing stops while one of\n"
       "those mods is loaded."),
    PICK("DynamicUpload", "Auto|0|1", "Auto",
         "Auto uses the upload improvement on supported Revival builds and graphics\n"
         "cards. It aims to reduce graphics pauses. This is a preview feature; set 0\n"
         "if you see picture problems. 1 also enables it when supported."),
    NUM("TraceFrames", 0, 32768, "0", "Developer diagnostic capture. Leave at 0 for normal play."),
    NUM("LogFirst", 0, 0, "6", "DisplayManager.log: log the first N frames. Display changes are always logged."),
    NUM("LogEvery", 0, 0, "0", "DisplayManager.log: then log every Nth frame (0 = never)."),
    NUM("FullscreenDelayMs", 0, 0, "0", "For automated tests: wait this many milliseconds before Fullscreen=1 acts."),
};
}

EFZ_CONFIG_EXPORT const EfzConfigSchema EFZ_ConfigSchema = {
    EFZ_CONFIG_SCHEMA_VERSION, sizeof(EfzConfigSetting), "DisplayManager.ini",
    sizeof(kSettings) / sizeof(kSettings[0]), kSettings,
};
