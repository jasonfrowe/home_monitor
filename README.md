# Home Monitor - RSS Feed Reader

This program fetches and displays RSS feeds on your RP6502 Picocomputer. It currently supports a local WeeWX weather station and the Slashdot news feed.

![RP6502 Monitor Screenshot](images/Screenshot.png)

It functions as a "Dashboard," automatically refreshing every 5 minutes and formatting the output with ANSI colors for readability.

## Features

- **Multi-Feed Support**: Switch between local weather and internet news feeds instantly.
- **Network Access**: Connects via the RP6502 `AT:` modem emulator (WiFi).
- **Dashboard Display**: Parses RSS data to display organized summaries (Weather Conditions vs News Headlines).
- **ANSI Graphics**: Formats output with colors (Cyan, Yellow, Green, White) and decodes HTML entities (e.g., degree symbols).
- **Auto-Refresh**: Updates data automatically every 5 minutes.
- **Keyboard Control**: Uses direct RIA hardware access for non-blocking input:
  - **[1]**: Switch to News
  - **[2]**: Switch to Weather
  - **[ESC]**: Exit program

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

## How It Works

1. **Modem Initialization**: Opens the `AT:` device and resets the modem emulator (`ATZ`, `ATE0`).
2. **TCP Connection**: Uses the Hayes command `ATDhostname:port` to open a raw TCP stream to the server.
3. **HTTP Request**: Sends a `GET` request with `Connection: close`.
4. **Data Retrieval**: Uses the standard library `read()` function (Kernel Block Read) to pull data into a 16KB global buffer. This is significantly faster than stack-based operations.
5. **Parsing & Filtering**:
   - Scans for specific tags (`<description>` for weather, `<title>` for news).
   - Filters out unwanted metadata (e.g., channel descriptions).
   - Decodes HTML entities (like `&#176;` for degrees).
6. **Input Handling**: Maps the USB HID keyboard state to XRAM address `0x8000`. The program directly reads the RIA hardware registers to detect key presses without blocking the update timer.

## Customization

To add or modify feeds, edit the `FeedConfig` structures in `src/main.c`:

```c
FeedConfig feed_custom = { 
    2,                  /* ID */
    "My Feed",          /* Display Name */
    "example.com",      /* Host */
    "80",               /* Port */
    "/rss.xml",         /* Path */
    "<title>",          /* Start Tag */
    "</title>",         /* End Tag */
    1                   /* Skip first item? (1=Yes) */
};
```

## Technical Details

- **Memory Management**: Uses a **16KB global buffer** (`BUFFER_SIZE`) to ensure full HTTP headers and RSS bodies are captured (Slashdot headers are large).
- **Keyboard Mapping**: The keyboard state is mapped to XRAM address **0x8000**.
  - *Note:* This address is chosen to avoid collisions with Video RAM (low memory) and the System Stack (high memory).
- **ANSI Codes**: The program uses standard ANSI escape codes for screen clearing (`\x1b[2J`) and text coloring.

## Troubleshooting

### "Error: Modem not ready"
- Ensure the `AT:` device is available.
- Verify WiFi is configured (`STATUS`).

### "Connection Failed" / "NO CARRIER"
- Verify the hostname is reachable.
- Try using an IP address instead of a hostname.
- Ensure the server is listening on port 80.
- **Note:** This program does not support HTTPS (SSL). It requires plain HTTP feeds.

### Screen shows "Happy Faces" or Garbage
- This indicates a memory collision. The keyboard XRAM address is likely overwriting Video Memory. Ensure `KEYBOARD_INPUT` is set to a safe address like `0x8000`.

## References

- [RP6502-RIA-W Documentation](https://picocomputer.github.io/ria_w.html)
- [RP6502-OS Documentation](https://picocomputer.github.io/os.html)
- [WeeWX Weather Software](https://weewx.com/)
```