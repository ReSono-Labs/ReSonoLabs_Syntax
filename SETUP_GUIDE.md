# DeskBot Setup Guide

Follow these steps to connect your DeskBot to Wi-Fi and pair it with your OpenClaw server.

## 1. Initial Wi-Fi Setup (Hotspot Mode)

When the device is fresh or cannot find a saved network:
1.  **Connect to Hotspot**: On your phone or computer, look for a Wi-Fi network named `s3_1_85c_setup` (or similar).
2.  **Enter Password**: The default password is **`abcdefgh`**.
3.  **Open Web Portal**: Open your browser and go to **`http://192.168.4.1`**.
4.  **Select Network**: Choose your home/office Wi-Fi from the list, enter your password, and click **Save Wi-Fi and Connect**.
5.  **Get Device IP**: Once the device connects, it will display its new IP address on its screen.

## 2. Unlocking the Control Panel

Once the device is on your network:
1.  **Open Control Panel**: In your browser, go to **`http://<DEVICE_IP>`** (replace with the IP shown on the device).
2.  **Get the Dev PIN**: On the DeskBot screen, **swipe down from the top** to open the Info Drawer. You will see a **6-digit Security PIN**.
3.  **Unlock**: Enter this PIN into the "Desk Bot Locked" section of the web portal and click **Unlock**.

## 3. OpenClaw Integration

To connect DeskBot to your OpenClaw assistant:
1.  **Configure Endpoint**: In the "OpenClaw Pairing" section of the web portal, enter your **OpenClaw Server IP/Host** and **Port**. Click **Save OpenClaw Endpoint**.
2.  **Add Gateway Token**: 
    -   Ensure your OpenClaw server's **Auth Mode** is set to **`token`**.
    -   You can find your **Gateway Token** in your OpenClaw server's `openclaw.json` file.
    -   Enter the token and click **Save Gateway Token**. The device will immediately attempt its first pairing.
3.  **Approve Pairing**: 
    -   Log into your **OpenClaw Server Dashboard**.
    -   Go to the **Nodes** section.
    -   Look for a new device pairing request titled "**DeskBot**".
    -   Click **Approve**.
4.  **Reconnect**: Return to the DeskBot web portal and click **Reconnect OpenClaw**.

Your DeskBot should now transition to "READY" and be able to receive voice commands!

---

### Tips
-   **Firmware**: The build is optimized for **V1 hardware** (PCM5101).
-   **Security**: The Dev PIN clears occasionally for security; just check the Info Drawer again if you are locked out of the web portal.
-   **Logs**: Use `idf.py monitor` while connected via USB if you need to debug pairing issues.
