# Changelog

## 1.1.1

- `LowLatencyWindow` now turns itself off on screens slower than the game
  (under 64 Hz, for example a 60 Hz laptop screen). On those screens the
  newer picture mode would slow the whole game down to the screen's speed,
  so the classic mode stays on and the game keeps running at full speed.
  On screens of 64 Hz and more nothing changes; the mod notes its choice
  in the log file.
- Clearer wording in the readme, the guide, and the settings file about
  when the newer picture mode is used.

## 1.1.0

- Fixed: after leaving fullscreen, the game window used to stay on top of
  all other windows even when another program was in front. It no longer
  does.
- Fixed: the fullscreen hotkey (`F11` / `Alt+Enter`) did nothing when the
  game window was on a monitor where fullscreen could not engage. The
  hotkey now works on every monitor: if true fullscreen is not possible
  there, the game goes borderless fullscreen on that monitor instead.
- New setting `LowLatencyWindow` (off by default): the picture reaches the
  screen sooner in window and borderless mode. See above for the 1.1.1
  screen-speed rule.
- New setting `HideCursor` (on by default): hides the mouse pointer while
  it is over the game window.
- New setting `FullscreenRefresh`: choose a refresh rate for fullscreen
  (`0` keeps the current one).
- Settings in `DisplayManager.ini` now use readable names, for example
  `FullscreenMode=Borderless` or `FullscreenMode=Exclusive` instead of
  numbers.
