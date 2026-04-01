# How to Build and Flash This Project

This project is built using the **Espressif IoT Development Framework (ESP-IDF)**. It is configured for the **ESP32-S3-Touch-LCD-1.85C** (and similar S3-based round displays).

## 🛠 Prerequisites

1.  **ESP-IDF SDK**: This project is currently using **ESP-IDF v6.1-dev** (Development Branch). 
    - *Note: While it likely works on v5.x, the current build environment is pointing to v6.1-dev.*
2.  **Hardware**: ESP32-S3-Touch-LCD-1.85C board connected via USB.

## 🚀 Quick Start (Linux & macOS)

### 1. Set Up Environment
Open your terminal and source the ESP-IDF export script:
```bash
. $HOME/esp/esp-idf/export.sh
```

### 2. Set the Target
Configure the project for the ESP32-S3 chip:
```bash
idf.py set-target esp32s3
```

### 3. Build, Flash, and Monitor
Run the following command to compile the code, upload it to your device, and open the serial monitor in one go:
```bash
idf.py build flash monitor
```

## 🪟 Windows (PowerShell/CMD)

### 1. Set Up Environment
Run the export script provided by your ESP-IDF installation:
```powershell
# PowerShell
. $env:IDF_PATH\export.ps1

# CMD
%IDF_PATH%\export.bat
```

### 2. Build and Flash
```bash
idf.py build
idf.py -p PORT flash monitor
```
*(Replace `PORT` with your COM port, e.g., `COM3`)*

## 📝 Key Commands

| Command | Description |
| --- | --- |
| `idf.py build` | Compiles the application, bootloader, and partition table. |
| `idf.py flash` | Uploads the binaries to the ESP32. |
| `idf.py monitor` | Opens the serial console to view logs (Ctrl+] to exit). |
| `idf.py menuconfig` | Opens the visual configuration menu for project settings. |
| `idf.py fullclean` | Removes all build artifacts for a fresh start. |

## ⚠️ Notes
- The project automatically uses `sdkconfig.defaults` and `partitions.csv` located in the root.
- If you notice I2C errors at boot, ensure your board is the **V1** version (PCM5101 based) as this build has been optimized for that architecture.
