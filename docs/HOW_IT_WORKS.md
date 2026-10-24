# How EfzDisplayManager works

Revival draws EFZ's 640x480 picture and scales it to the game window.
DisplayManager adjusts that last step so the picture keeps its proportions,
with bars around it where needed. Optional filters change how enlarged pixels
look.

## Settings shared with Revival

Revival still reads `EfzRevival.ini` and creates the starting window and
backbuffer using its settings. DisplayManager keeps that starting size and
position. It does not copy those settings into a second file or change
Revival's file.

After you resize the window, DisplayManager adjusts the backbuffer to match.
Returning to the original window size restores Revival's original backbuffer
size. Set `ResizeBackBuffer=0` to keep Revival's fixed backbuffer throughout.

DisplayManager's own options live in `DisplayManager.ini`. It creates that file
if needed and fills in missing or invalid options at startup. Valid choices
and unrelated entries are kept. Close the game before editing either file.

## Fullscreen and game speed

New settings start in a window. Press `F11` or `Alt+Enter` to switch to exclusive
fullscreen. Existing settings keep the fullscreen type you chose.

EFZ keeps its own timing, which targets about 64 frames per second.
`VSync=Auto` allows tearing in exclusive fullscreen so a 60 Hz screen does
not limit the game to one frame per refresh. `VSync=0` does the same.
`VSync=1` waits for refreshes and requires a display mode of at least 64 Hz;
that waiting can add delay.

If exclusive mode is unavailable, the existing fallback uses borderless.
The log records the mode that actually started. Borderless is not a substitute
for testing exclusive fullscreen performance.

## Sending the picture to the graphics card

`DynamicUpload=Auto` enables the upload improvement on supported graphics
cards with Revival 2020 or original 1.02j. It gives the graphics driver a way
to accept the current picture without waiting for the previous upload buffer.
The picture is still uploaded and submitted in the same frame iteration.
The game does not deliberately hold it for the next frame or skip presentations
to keep a counter high.

This is a preview. It leaves the game's timing, input, replay and rollback
code in place, but graphics performance and compatibility during live matches
still need testing on more machines. Set `DynamicUpload=0` if it causes picture
problems. Other Revival builds keep their original upload path.

## Resizing with other mods

Changing the display requires recreating graphics resources. DisplayManager
asks supported overlay mods to release their resources first, then rebuild
them when the display is ready. Older overlay mods may prevent resizing.

Overlay authors can use the [compatibility guide](COMPATIBILITY.md) to add
this support. That guide also covers fullscreen changes and recovery after
switching away from the game.

## Supported versions and troubleshooting

The mod checks the exact Revival DLL before changing its running code.
It supports the known 2020, 1.02f, 1.02g, 1.02h, 1.02i and two 1.02j builds.
An unknown build keeps its native display behavior. The DLL on disk is never
modified.

`DisplayManager.log` records the detected build, settings repairs and display
changes. Attach it when reporting a problem. The [README](../README.md) covers
installation, controls and common settings.
