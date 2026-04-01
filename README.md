# ReSono Labs Syntax Firmware

## Required Hardware

This firmware build is specifically for the **Waveshare ESP32-S3 1.85C Round LCD with Speaker**:

- Product page: `https://www.waveshare.com/esp32-s3-touch-lcd-1.85c.htm`
- You can buy it directly from Waveshare, and it is also available from other online sellers.

Important revision note:

- This project currently targets **hardware V1** of the `ESP32-S3-Touch-LCD-1.85C`.
- Waveshare documents a **V1 / V2 hardware split**. On V1, the audio path uses a `PCM5101APWR` decoder. On V2, Waveshare changed the audio path to `ES8311` + `ES7210`, changed the microphone design, and changed several board-level pin assignments.
- Waveshare's current version notes also show pin differences including `GPIO2` (`MIC_WS` on V1, `I2S_MCLK` on V2), `GPIO10` / `GPIO11`, and `GPIO15`.
- This firmware's current board support and audio platform are wired for the **V1 board-level implementation**, so **try to get V1 if possible**.
- We have **not tested this build on V2** yet. Based on the documented differences, V2 should likely require only **small board support package and audio driver changes** to run, but that port has not been completed here.
- If enough developers receive V2 hardware, I will consider purchasing a V2 board and porting the firmware. If you receive V2 first, you can also make those board-level changes yourself.

If you need to adapt this firmware to another hardware revision or another target board, start with [APPLICATION_LAYER_PORTING_GUIDE.md](/home/chris/Documents/ambitious/esp32/APPLICATION_LAYER_PORTING_GUIDE.md).

ReSono Labs Syntax is the ESP32-S3 firmware for the ReSono Labs Syntax voice device. It handles first-boot Wi-Fi provisioning, the local web control panel, touchscreen/device UI, audio capture and playback, OTA upload, and the live OpenClaw device bridge used for real-time voice sessions and deferred task results.

This repo was also structured so the application/runtime layer can be reused on other hardware targets. The current device is one implementation of the stack, not the only possible one.

This project is intended to work together with the ReSono Labs OpenClaw Bridge plugin:

- Bridge plugin: `https://github.com/ReSono-Labs/ReSono-Labs-OpenClaw-Bridge.git`

The firmware and the bridge plugin are a matched system. The device can boot without the plugin, but OpenClaw voice sessions and result delivery will not work correctly until the plugin is installed and configured on the OpenClaw host.

## What This Project Does

At a high level, this firmware provides:

- Wi-Fi setup through a temporary device hotspot and setup portal
- A locked local web UI for device status, Wi-Fi updates, OTA upload, and OpenClaw pairing
- A touchscreen UI with an info drawer that shows the Dev PIN, active SSID, and IP address
- Audio capture, audio playback, and live session state handling
- OpenClaw websocket connectivity, device pairing, and `device-bridge.*` session/result transport
- Local storage for Wi-Fi credentials, settings, and OpenClaw tokens

## Architecture Summary

The main runtime starts in [main/app_main.c](/home/chris/Documents/ambitious/esp32/main/app_main.c) and runs the application loop in [app/app.c](/home/chris/Documents/ambitious/esp32/app/app.c).

Important subsystems:

- `app/`
  Core application state machine, mic/session control, replay handling, and bootstrap logic.
- `platform/network/`
  Wi-Fi station + setup AP behavior, scanning, and IP reporting.
- `services/wifi_provisioning/`
  First-time setup portal served at `http://192.168.4.1/` while the setup AP is active.
- `ui/web_shell/`
  Main local control panel shown after Wi-Fi is configured.
- `runtime/provider/provider_transport/`
  OpenClaw websocket transport and `device-bridge.*` method/event integration.
- `runtime/provider/provider_web_bridge/`
  OpenClaw pairing controls embedded into the local web UI.
- `ui/drawer/` and `ui/shell/`
  On-device drawer/status UI including the Dev PIN, SSID, and IP address.

For a detailed guide on reusing the application layer on other hardware, see [APPLICATION_LAYER_PORTING_GUIDE.md](/home/chris/Documents/ambitious/esp32/APPLICATION_LAYER_PORTING_GUIDE.md).

## Prerequisites

Before setting up a device, complete these prerequisites:

1. Install and configure the ReSono Labs OpenClaw Bridge plugin on your OpenClaw host.
   Repo: `https://github.com/ReSono-Labs/ReSono-Labs-OpenClaw-Bridge.git`
2. Make sure your OpenClaw gateway is reachable from the device over your LAN.
3. Build and flash this firmware with **ESP-IDF v6**.

ESP-IDF requirement:

- `ESP-IDF v6` is required.
- Do not use ESP-IDF v5 or earlier for this project.
- See [BUILD_AND_FLASH.md](/home/chris/Documents/ambitious/esp32/BUILD_AND_FLASH.md) for build and flash details.

## Setup Flow

The recommended order is:

1. Install the ReSono Labs OpenClaw Bridge plugin on the OpenClaw server.
2. Build and flash ReSono Labs Syntax firmware to the device.
3. Connect to the device hotspot and provision Wi-Fi.
4. Reconnect to the device on your LAN through its local web UI.
5. Unlock the UI with the Dev PIN from the device info drawer.
6. Configure the OpenClaw host/port and gateway token.
7. Approve the device pairing request in OpenClaw.
8. Reconnect OpenClaw from the device UI and verify the device reaches `READY`.

For the step-by-step walkthrough, see [SETUP_GUIDE.md](/home/chris/Documents/ambitious/esp32/SETUP_GUIDE.md).

## First-Time Provisioning

When the device has no saved Wi-Fi credentials, it starts a setup access point and serves a provisioning page.

Behavior from code:

- Hotspot SSID format: `ReSono Labs Syntax <last-two-mac-bytes>`
- Hotspot password: `abcdefgh`
- Setup portal: `http://192.168.4.1/`

The setup page lets you:

- scan nearby Wi-Fi networks
- choose a scanned SSID or enter one manually
- save Wi-Fi credentials
- trigger an immediate station connect attempt

After credentials are saved, the device reboots and continues setup using the normal web control UI.

## Local Web Control UI

Once the device joins your Wi-Fi network, browse to `http://<device-ip>`.

The web UI provides:

- device status
- Wi-Fi scan/save
- OTA upload
- OpenClaw pairing controls

The control UI is locked behind a 6-digit Dev PIN. The PIN is shown on the device in the info drawer alongside:

- active SSID
- IP address
- volume
- power status

Use the drawer to retrieve the current IP and Dev PIN before unlocking the web UI.

## OpenClaw Integration

The firmware uses the OpenClaw device auth flow and talks to the ReSono Labs OpenClaw Bridge plugin over the `device-bridge.*` namespace.

Once configured, the device:

- opens live bridge sessions over websocket
- streams text/audio turns through `device-bridge.session.*`
- subscribes for deferred task results through `device-bridge.results.*`
- stores and reuses OpenClaw device tokens locally after pairing

The local web UI includes controls to:

- save OpenClaw host and port
- save the OpenClaw gateway token
- reconnect OpenClaw
- forget the saved device token
- do a full OpenClaw reset

## Development Notes

- This repo currently targets the ESP32-S3 round-display hardware path used by ReSono Labs Syntax.
- USB serial monitoring remains the fastest way to debug setup and pairing issues.
- Runtime logs are useful during first pairing, Wi-Fi setup, and bridge transport changes.

## Related Docs

- [SETUP_GUIDE.md](/home/chris/Documents/ambitious/esp32/SETUP_GUIDE.md)
- [BUILD_AND_FLASH.md](/home/chris/Documents/ambitious/esp32/BUILD_AND_FLASH.md)
- [APPLICATION_LAYER_PORTING_GUIDE.md](/home/chris/Documents/ambitious/esp32/APPLICATION_LAYER_PORTING_GUIDE.md)

## License

This project is licensed under the PolyForm Noncommercial License 1.0.0. See [LICENSE](/home/chris/Documents/ambitious/esp32/LICENSE).
