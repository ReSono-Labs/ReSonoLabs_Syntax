# Application Layer Porting Guide

This document explains how to reuse the ReSono Labs Syntax application/runtime stack on other hardware.

The project was intentionally built as a layered system so the OpenClaw-connected voice application can be moved to other ESP32-S3 devices, or to other embedded hardware with similar capabilities, without rewriting the entire product from scratch.

## Goal

The goal is not to clone the exact ReSono Labs Syntax hardware design.

The goal is to preserve the reusable application layer:

- device state machine
- live OpenClaw bridge/session behavior
- task-result handling
- provisioning and control concepts
- settings, security, OTA, and local services

while replacing the hardware-specific layers:

- display
- touch/input
- audio paths
- power reporting
- board constants
- visual UI layout

## How The Repo Is Layered

At a practical level, the repo splits into these layers:

### 1. Core State / Presentation

These are the most portable pieces.

- `core/state/`
- `core/presentation/`
- `core/runtime_control/`

These components describe:

- high-level app state
- presentation-state mapping
- runtime/provider control interfaces

They are not tied to the current LCD, touch controller, or audio codec.

### 2. Provider / Bridge Runtime

This is also highly reusable if the new device is still meant to talk to OpenClaw.

- `runtime/provider/provider_session/`
- `runtime/provider/provider_terminal/`
- `runtime/provider/provider_transport/`
- `runtime/provider/provider_runtime_api/`
- `runtime/provider/provider_state_bridge/`
- `runtime/provider/provider_ui_bridge/`
- `runtime/provider/provider_web_bridge/`
- `runtime/provider/provider_storage/`

This layer is the OpenClaw-facing application runtime. It manages:

- websocket connection lifecycle
- pairing
- session open/close
- voice turn transport
- deferred task results
- OpenClaw status exposure to UI/web layers

If your new device should still work with the ReSono Labs OpenClaw Bridge plugin, this layer should usually stay.

### 3. Services

These are reusable depending on product goals:

- `services/security/`
- `services/settings/`
- `services/wifi_setup/`
- `services/wifi_provisioning/`
- `services/ota/`
- `services/orb/`

These are not tied tightly to the 1.85C display hardware.

In most ports, you can keep most of them.

### 4. App Orchestration

- `app/app.c`
- `app/board_registry.c`

This is the orchestration layer that composes everything together.

It is portable in concept, but not plug-and-play portable in implementation.

Why:

- it contains the app state machine
- it manages mic ring buffers and turn timing
- it decides how tap-to-listen works
- it coordinates UI, provider runtime, and audio behavior

This is the layer you will most likely adapt, not fully replace.

### 5. Platform Layer

This is the least portable part.

- `platform/audio/`
- `platform/display/`
- `platform/input/`
- `platform/network/`
- `platform/power/`
- `platform/storage/`
- `platform/web_server/`

These are hardware and board-integration adapters.

They are where the current project touches:

- GPIOs
- I2C/SPI buses
- display driver details
- touch controller details
- mic/speaker paths
- battery/power hardware
- NVS/storage
- onboard web server lifecycle

For a different device, assume these need review or replacement.

### 6. UI Layer

This is only reusable if the new device has a very similar visual and interaction model.

- `ui/shell/`
- `ui/drawer/`
- `ui/notification_tray/`
- `ui/widgets/`
- `ui/web_shell/`
- `ui/web_common/`

If the new device:

- has the same size/shape display
- uses the same interaction model
- still needs the same local web control panel

then parts of this layer can be reused.

Otherwise, plan to rewrite large parts of it.

### 7. Board Definitions

- `boards/`

These are not portable. They are the current hardware declaration and support layer.

This is where you create the new board profile and hardware constants for another device.

## What Can Usually Be Reused

These layers are the best candidates to carry forward into another device project:

- `core/state`
- `core/presentation`
- `core/runtime_control`
- most of `runtime/provider/*`
- `services/security`
- `services/settings`
- `services/wifi_setup`
- `services/wifi_provisioning`
- `services/ota`
- parts of `app/app.c` logic

If the new product still:

- connects to OpenClaw
- runs live voice sessions
- uses result replay / deferred work
- uses local Wi-Fi setup
- exposes a local admin web panel

then most of the actual product logic can stay.

## What Usually Must Change

These areas should be assumed to require real porting work:

- `boards/*`
- `platform/audio/*`
- `platform/display/*`
- `platform/input/*`
- `platform/power/*`
- UI layout and rendering in `ui/*`
- board-specific bootstrap behavior in `app/board_registry.c`

In many ports, the code in `app/app.c` also needs to be trimmed or adapted because its event flow assumes the current device’s:

- tap interaction model
- mic buffering strategy
- speaker timing
- drawer behavior
- UI state transitions

## What Depends On Matching Hardware

### Display

If the new device has:

- no display
- a different resolution
- a rectangular screen
- different refresh constraints

then `platform/display`, `ui/shell`, `ui/drawer`, and `ui/widgets` all need adaptation.

### Input

If the new device uses:

- no touch
- buttons instead of touch
- a wake word instead of tap-to-listen
- rotary or gesture input

then `platform/input` and parts of `app/app.c` need new behavior.

### Audio

If the mic, codec, I2S configuration, sample rate, or speaker path changes, you should expect work in:

- `platform/audio`
- the ring-buffer and timing logic in `app/app.c`

This is one of the most sensitive parts of the port.

### Power / Battery

Battery telemetry and USB/power reporting are device-specific. That impacts:

- `platform/power`
- drawer/status UI
- any power-aware UX

## Resource Constraints Matter

Porting the application layer does not mean every target is a good fit.

Before reusing this stack on a new device, evaluate:

- available PSRAM
- internal RAM
- CPU headroom
- display bandwidth
- audio DMA/I2S constraints
- websocket/network stability

This project uses:

- real-time audio streaming
- JSON/websocket processing
- local web UI
- LVGL UI
- background state and buffer management

Devices with significantly less RAM, no PSRAM, or weaker audio/display pipelines may need feature reduction.

## The Cleanest Porting Strategy

The cleanest way to port this project is:

1. Keep the OpenClaw-facing provider/runtime layer.
2. Keep the core state/presentation/runtime-control layer.
3. Keep the reusable services where possible.
4. Create a new board definition and support package.
5. Reimplement platform adapters for the new hardware.
6. Decide whether to:
   - keep the existing UI layer,
   - keep only the web UI,
   - or replace both device UI and web UI.
7. Adapt `app/app.c` to the new interaction model.

## Practical Reuse Options

### Option A: Same Product Model, Different Board

Use this when the new device is still:

- a voice terminal
- OpenClaw-connected
- touch-driven or similarly interactive
- using onboard display and audio

In this case, you can usually preserve most of the application/runtime stack and just swap:

- board profile
- hardware constants
- platform adapters
- some UI sizing/layout

### Option B: Same Backend, Different Frontend

Use this when the new device still talks to OpenClaw, but interaction is very different.

Examples:

- button-based wearable
- speaker-only device
- kiosk with a different screen shape

In this case, keep:

- provider/runtime
- core
- services

and rewrite:

- input model
- local UI
- relevant app orchestration behavior

### Option C: Headless / Minimal Device

If the new device has no display or a very limited UI, you may still reuse:

- OpenClaw bridge runtime
- Wi-Fi setup pieces
- OTA/settings/security

but you should expect to remove or heavily reduce:

- `ui/*`
- display platform
- drawer-based status assumptions

## Suggested Boundaries For A New Device Port

If starting a second hardware target, a good mental split is:

Reusable application/runtime layer:

- `core/*`
- `runtime/provider/*`
- selected `services/*`
- reusable parts of `app/app.c`

Device-specific layer:

- `boards/<new_board>/`
- `platform/*`
- `ui/*`
- any board-specific branching in `app/board_registry.c`

## Why This Repo Was Built This Way

This firmware was not only built to power the current ReSono Labs Syntax hardware.

It was also structured so developers could:

- branch the application/runtime layer off
- bring up different hardware around it
- keep the OpenClaw bridge behavior intact
- swap device-specific audio, display, input, and power implementations

That is why the repo separates:

- core state/presentation
- provider runtime
- services
- platform adapters
- board support
- UI

The separation is not perfect, because `app/app.c` still carries a lot of orchestration logic, but the architecture is already much closer to a portable embedded application stack than to a one-board monolith.

## Recommended Next Step For Future Ports

If this project will actually support multiple hardware targets, the next architectural improvement should be:

- reduce hardware assumptions inside `app/app.c`
- define a clearer device capability contract between `app/` and `platform/`
- move more interaction policy behind board or platform ops

That would make the application layer more cleanly reusable across:

- watch-style devices
- round-display ESP32 devices
- rectangular display devices
- minimal audio nodes
- future non-Syntax hardware
