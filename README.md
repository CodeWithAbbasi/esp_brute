
# 📖 ESP Brute – Device Usage Guide (ESP32 DevKit v1)


# ESP Brute

ESP32-based tool to validate initial network access in authorized test environments.  
Designed for quick field testing (ESP32 DevKit v1, ESP-IDF v4.4.3). Includes MAC randomization, configurable attempt limits, and a simple demo wordlist.

> **Tagline:** Getting on the network is often the first threat — ESP Brute helps pentesters validate that entry point.  
> **Important:** Use only in labs or where you have explicit written permission.

---

## Features
- Scan nearby Wi-Fi APs (SSID, BSSID, RSSI)
- Iterate APs with a wordlist and attempt connections
- MAC randomization before each attempt
- Per-AP and global attempt limits
- Simple demo wordlist built into source (option to expand via filesystem/SD in future)

---

**Permissions**
- **Written authorization** from the network owner(s) is required for any testing. Do NOT use this on networks you do not own or do not have explicit permission to test.

---

## Quick start — build & flash (ESP-IDF)

## 2. Clone the Repository
git clone https://github.com/CodeWithAbbasi/esp_brute.git
cd esp_brute
## 3. Build & Flash

1. Set the target:
   idf.py set-target esp32
   
2. Configure the project (optional):

  bash
   idf.py menuconfig

3. Build the firmware:


   idf.py build
  
4. Flash and monitor (replace `<PORT>` with your serial port, e.g. COM3 on Windows or /dev/ttyUSB0 on Linux):

  bash
   idf.py -p <PORT> flash monitor (PORT can be found using device manager)
 

To exit monitor: press `Ctrl+]`.


## 4. What Happens After Flashing

* ESP32 scans for nearby Wi-Fi access points.
* For each AP, it attempts passwords from the built-in wordlist.
* The MAC address is randomized before each attempt.
* If a password is found, it will be printed to the serial monitor.

Example output:

```
I (1234) ESP_BRUTE: Scanning Wi-Fi...
0: MyHomeWiFi (RSSI -45) BSSID 24:0A:C4:XX:XX:XX
I (2345) ESP_BRUTE: Attempting connect -> SSID:"MyHomeWiFi" PASS:"password123"
I (3456) ESP_BRUTE: Connection failed
*** SUCCESS: SSID: MyHomeWiFi  PASSWORD: secretpass
```

---

### 5 Wordlist size & run-time estimates

This firmware can hold **up to ~20,000 hardcoded passwords** on an ESP32 DevKit v1 (flash and build permitting).  
**Important:** each connection attempt (handshake + timeout + polite delay) takes about **5 seconds** on average, so large lists take a long time to run.

Estimated total run-times (at ~5 s per attempt):
- 1,000 passwords: 1 hour 23 minutes 20 seconds (5,000 seconds)  
- 10,000 passwords: 13 hours 53 minutes 20 seconds (50,000 seconds)  
- 20,000 passwords: 27 hours 46 minutes 40 seconds (100,000 seconds)

These numbers are per-AP: if you try a full list against multiple APs the time multiplies accordingly.

#### Tips to reduce total time
- Reduce `CONNECT_TIMEOUT_MS` and `POLITE_DELAY_MS` in `lab_dict_device_clean_mac.c` (but beware of false negatives and AP lockouts).  

the array in wich passwrod is to be  add 
```
const char *wordlist[] = {
    "password123",
    "12345678",
    "letmein"
};

```

Then rebuild and flash:

idf.py build
idf.py -p <PORT> flash monitor


---

## 6. Stopping, resuming, and post-success behavior

* The example stops after printing `*** SUCCESS`. Modify `flow_task` to change behavior (e.g., continue scanning, log results).
* To stop: close serial monitor or press EN (reset) on board.
* To resume: power-cycle or re-flash.

---

## 7. Troubleshooting

* **No serial output:** Confirm correct COM port and USB drivers.
* **Flashing failed:** Check USB cable (must be data-capable), drivers, and that you set the correct port. Some boards require holding BOOT during flashing.
* **Frequent reboots / instability:** Try reducing wordlist size or lowering attempt limits in the source.

---

⚠️ **Disclaimer**: This project is for **educational and authorized security research only**. Do not use it on networks you do not own or have explicit permission to test.

```
