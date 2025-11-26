# Weather Monitor - RSS Feed Reader

This program fetches and displays current weather conditions from a WeeWX weather station RSS feed on your local network using the RP6502 Picocomputer.


![Weather Monitor Screenshot](images/Screenshot.png)

It functions as a "Dashboard," automatically refreshing every 5 minutes and formatting the output with ANSI colors for readability.

## Features

- **Network Access**: Connects to WeeWX weather station via the RP6502 `AT:` modem emulator (WiFi).
- **Dashboard Display**: Parses RSS data to display only "Current Conditions" and "Daily Summary".
- **ANSI Graphics**: Formats output with colors (Cyan, Yellow, Green, White) and decodes HTML entities (e.g., degree symbols).
- **Auto-Refresh**: Updates weather data automatically every 5 minutes.
- **Keyboard Control**: Uses direct RIA hardware access to scan the USB keyboard; press **ESC** to exit the loop immediately.

## Prerequisites

### WiFi Configuration

Before running this program, you need to configure WiFi on your RP6502-RIA-W:

1. From the RP6502 monitor console, configure your WiFi network:
   ```
   SET SSID your_network_name
   SET PASS your_network_password
   SET RF 1
   ```

2. Verify WiFi is connected:
   ```
   STATUS
   ```
   You should see WiFi connected and an IP address assigned.

### Build Environment

- **CMake**: Build system.
- **cc65**: 6502 Cross Compiler.
- **RP6502 SDK**: Libraries for the target platform.

## Building

```bash
cd /Users/rowe/home_monitor/home_monitor
cmake --build build
```

This produces `build/homemonitor.rp6502` ready to run on your RP6502.

## Running

1. Copy `build/homemonitor.rp6502` to your RP6502 storage (USB drive).
2. From the RP6502 monitor, run:
   ```
   load homemonitor.rp6502  
   reset  
   ```
3. To exit the program, press **ESC**.

## How It Works

1. **Modem Initialization**: Opens the `AT:` device and resets the modem emulator (`ATZ`, `ATE0`).
2. **TCP Connection**: Uses the Hayes command `ATDweatherpi.home.arpa:80` to open a raw TCP stream to the server.
3. **HTTP Request**: Sends a `GET /weewx/rss.xml HTTP/1.1` request with `Connection: close`.
4. **Data Retrieval**: Reads data from the RP6502 XSTACK. Since the XSTACK is LIFO (Last-In, First-Out), data is popped into a large global buffer (8KB) to reconstruct the stream.
5. **Parsing & Filtering**:
   - Scans for `<description>` tags.
   - Filters out RSS channel titles and Monthly/Yearly summaries to reduce clutter.
   - Decodes HTML entities (like `&#176;` for degrees).
6. **Input Handling**: Maps the USB HID keyboard state to XRAM address `0xEC20`. The program directly reads the RIA hardware registers to detect the `ESC` key without blocking the update timer.

## Customization

To fetch from a different RSS feed, modify these lines in `src/main.c`:

```c
/* Change the hostname/IP */
modem_send(fd, "ATD192.168.1.50:80\r\n");

/* Change the URL path */
modem_send(fd, "GET /feed.rss HTTP/1.1\r\n");

/* Change the Host header */
modem_send(fd, "Host: 192.168.1.50\r\n");
```

## Technical Details

- **Memory Management**: Uses an 8KB global buffer (`BUFFER_SIZE`) to ensure full HTTP headers and XML bodies are captured. This is allocated globally to avoid overflowing the 256-byte local stack limit.
- **Keyboard Mapping**: The keyboard state is mapped to XRAM address **0xEC20**.
  - *Note:* Do not change this to low memory (e.g., `0x1000`) or high memory (`0xFF00`) without checking for collisions with Video RAM or the System Stack.
- **ANSI Codes**: The program uses standard ANSI escape codes for screen clearing (`\x1b[2J`) and text coloring.

## Troubleshooting

### "Error: Modem not ready"
- Ensure the `AT:` device is available.
- Verify WiFi is configured (`STATUS`).

### "Connection Failed" / "NO CARRIER"
- Verify `weatherpi.home.arpa` is reachable from your network.
- Try using an IP address instead of a hostname in the `ATD` command.
- Ensure the server is listening on port 80.

### Screen shows "Happy Faces" or Garbage
- This indicates a memory collision. The keyboard XRAM address is likely overwriting Video Memory. Ensure `KEYBOARD_INPUT` is set to a safe address like `0xEC20` or `0x8000`.

## References

- [RP6502-RIA-W Documentation](https://picocomputer.github.io/ria_w.html)
- [RP6502-OS Documentation](https://picocomputer.github.io/os.html)
- [WeeWX Weather Software](https://weewx.com/)
