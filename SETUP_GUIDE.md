# ReSono Labs Syntax Setup Guide

## Required Hardware

This build is specifically for the **Waveshare ESP32-S3 1.85C Round LCD with Speaker**:

- Product page: `https://www.waveshare.com/esp32-s3-touch-lcd-1.85c.htm`
- You can buy it directly from Waveshare, and it is also commonly available through online sellers.

Before you buy one, read this carefully:

- This project currently targets **V1** of the `ESP32-S3-Touch-LCD-1.85C`.
- Waveshare documents a **V1 / V2 split**. V1 uses a `PCM5101APWR` audio decoder. V2 switches to `ES8311` + `ES7210`, changes the microphone path, and changes several board-level pin assignments.
- Documented Waveshare pin differences include `GPIO2`, `GPIO10`, `GPIO11`, and `GPIO15`, which matters for the current audio and peripheral wiring.
- We have **not tested this firmware on V2**. It should likely only need **small board support package and audio driver changes** to run on V2, but that port is not part of this repo today.
- Recommendation: **try to get V1 hardware**.
- If enough developers receive V2 boards, I will consider buying one and porting the firmware. If you receive V2 first, you can also make the board-level changes yourself.

If you need to adapt the runtime to another device or hardware revision, see [APPLICATION_LAYER_PORTING_GUIDE.md](/home/chris/Documents/ambitious/esp32/APPLICATION_LAYER_PORTING_GUIDE.md).

Follow these steps to connect your ReSono Labs Syntax device to Wi-Fi and pair it with your OpenClaw server.

Important: install the ReSono Labs OpenClaw Bridge plugin before setting up the device.

- Bridge plugin repo: `https://github.com/ReSono-Labs/ReSono-Labs-OpenClaw-Bridge.git`

The device firmware and the bridge plugin are designed to be used together. If the plugin is not installed first, OpenClaw pairing and voice sessions will not complete correctly.

## 0. Prerequisites

Before powering through device setup:

1. Install the ReSono Labs OpenClaw Bridge plugin on your OpenClaw host.
2. Confirm your OpenClaw gateway is reachable on your LAN.
3. Build and flash this firmware with **ESP-IDF v6**.

Build and flash reference:

- See [BUILD_AND_FLASH.md](/home/chris/Documents/ambitious/esp32/BUILD_AND_FLASH.md)

## 1. Initial Wi-Fi Setup (Hotspot Mode)

When the device is fresh or cannot find a saved network:
1.  **Connect to Hotspot**: On your phone or computer, look for a Wi-Fi network named `ReSono Labs Syntax XXYY`, where `XXYY` matches the last two MAC bytes shown by the device.
2.  **Enter Password**: The default password is **`abcdefgh`**.
3.  **Open Web Portal**: Open your browser and go to **`http://192.168.4.1`**.
4.  **Select Network**: Choose your home/office Wi-Fi from the list, enter your password, and click **Save Wi-Fi and Connect**.
5.  **Wait for Reboot / Reconnect**: After credentials are saved, the device reboots and attempts to join the selected Wi-Fi network.

## 2. Unlocking the Control Panel

Once the device is on your network:
1.  **Get Device IP**: On the ReSono Labs Syntax screen, pull down the top info drawer to view the current **IP address**.
2.  **Open Control Panel**: In your browser, go to **`http://<DEVICE_IP>`**.
3.  **Get the Dev PIN**: In the same info drawer, note the **6-digit Dev PIN**.
4.  **Unlock**: Enter this PIN into the "Device Locked" section of the web portal and click **Unlock**.

## 3. OpenClaw Integration

To connect ReSono Labs Syntax to your OpenClaw assistant:
1.  **Configure Endpoint**: In the "OpenClaw Pairing" section of the web portal, enter your **OpenClaw Server IP/Host** and **Port**. Click **Save OpenClaw Endpoint**.
2.  **Add Gateway Token**: 
    -   Ensure your OpenClaw server's **Auth Mode** is set to **`token`**.
    -   You can find your **Gateway Token** in your OpenClaw server's `openclaw.json` file.
    -   Enter the token and click **Save Gateway Token**. The device will immediately attempt its first pairing.
3.  **Approve Pairing**: 
    -   Log into your **OpenClaw Server Dashboard**.
    -   Go to the device pairing / nodes area in OpenClaw.
    -   Look for a new device pairing request titled "**ReSono Labs Syntax**".
    -   Click **Approve**.
4.  **Reconnect**: Return to the ReSono Labs Syntax web portal and click **Reconnect OpenClaw**.

Your ReSono Labs Syntax device should now transition to "READY" and be able to receive voice commands!

---

### Tips
-   **Firmware**: The build targets **V1 hardware** (`PCM5101APWR` audio path).
-   **Security**: The Dev PIN clears occasionally for security; just check the Info Drawer again if you are locked out of the web portal.
-   **Logs**: Use `idf.py monitor` while connected via USB if you need to debug pairing issues.
-   **Plugin First**: If OpenClaw pairing does not complete, verify the ReSono Labs OpenClaw Bridge plugin is installed and active before debugging the device.
