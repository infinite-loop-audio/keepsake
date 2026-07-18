# Repo Authority Map

Status: active
Owner: Infinite Loop Audio
Updated: 2026-07-16

## Ownership

| Owner | Owns | Does not own |
|---|---|---|
| Keepsake | CLAP descriptors, scan/cache, bridge lifecycle, non-rendering host placeholder and native-editor reopen action, bridge-owned native editor window, stable editor title | host screenshot UI, Screen Recording permission, host-specific plugin resolution, Soundcheck lifecycle |
| CLAP host | normal CLAP loading, host editor window, parent `NSView`, generic discovery/capture of auxiliary plugin windows | Keepsake bridge internals, legacy plugin input synthesis |
| Soundcheck | the same CLAP host contract as other hosts; generic native-window screenshots for all applicable plugins | a Keepsake-only app, dylib, helper protocol, or alternate plugin-loading topology |
| Legacy plugin | native editor rendering, input, modal windows, plugin-driven resize | host placeholder and screenshot policy |
| macOS | AppKit windowing, ScreenCaptureKit, TCC permission | product-specific window selection policy |

## Boundary Rule

Keepsake must remain useful in any conforming CLAP host without host-specific
code. Soundcheck may use public plugin IDs and generic host facilities, but it
must not become a runtime dependency or privileged companion.

## Screenshot Flow

1. The host opens Keepsake through ordinary CLAP GUI calls.
2. Keepsake attaches its placeholder and opens the native editor.
3. The host observes the newly visible auxiliary window.
4. The host captures that window through its general screenshot system.

No captured frame returns to Keepsake or the CLAP parent view.

## Next Task

Prove the boundary in Soundcheck and REAPER, then implement screenshot capture
only on the Soundcheck side as generic host behavior.
