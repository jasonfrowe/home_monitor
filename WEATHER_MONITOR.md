# Weather Monitor - RSS Feed Reader

This program fetches and displays current weather conditions from a WeeWX weather station RSS feed on your local network.

## Features

- Connects to WeeWX weather station via WiFi
- Fetches RSS feed over HTTP
- Parses and displays weather conditions
- Uses Hayes modem emulation for network access

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

## Building

```bash
cd /Users/rowe/home_monitor/home_monitor
cmake --build build
```

This produces `build/homemonitor.rp6502` ready to run on your RP6502.

## Running

1. Copy `build/homemonitor.rp6502` to your RP6502 storage (USB drive)
2. From the RP6502 monitor, run:
   ```
   RUN homemonitor.rp6502
   ```

## How It Works

1. **Modem Initialization**: Opens `/dev/modem` and resets the Hayes modem emulator
2. **TCP Connection**: Uses AT command `ATDweatherpi.home.arpa:80` to connect via raw TCP
3. **HTTP Request**: Sends `GET /weewx/rss.xml HTTP/1.0` request
4. **RSS Parsing**: Extracts content from `<description>` tags in the RSS feed
5. **Display**: Prints weather conditions to console
6. **Cleanup**: Hangs up the modem connection

## Customization

To fetch from a different RSS feed, modify these lines in `src/main.c`:

```c
/* Change the hostname/IP */
send_at_command(fd, "ATDweatherpi.home.arpa:80\r\n");

/* Change the URL path */
strcpy(g_buffer, "GET /weewx/rss.xml HTTP/1.0\r\n");

/* Change the Host header */
strcpy(g_buffer, "Host: weatherpi.home.arpa\r\n\r\n");
```

## Troubleshooting

### "Could not open modem"
- Make sure you have RP6502-RIA-W (not just RIA)
- Verify WiFi is configured and enabled (`STATUS` command)

### Connection fails
- Verify `weatherpi.home.arpa` is reachable from your network
- Try using IP address instead: `ATD192.168.1.100:80`
- Check that WeeWX is running and serving RSS at `/weewx/rss.xml`

### No weather data displayed
- RSS feed may be in a different format
- Try viewing the raw feed in a browser first
- Adjust parsing logic if needed

## Technical Notes

- Uses global buffers (128 bytes each) to avoid stack overflow (256 byte limit)
- Implements simple string-based RSS parsing
- AT modem commands follow Hayes standard
- HTTP/1.0 for simplicity (no chunked encoding)
- Raw TCP connection (not full telnet protocol)

## References

- [RP6502-RIA-W Documentation](https://picocomputer.github.io/ria_w.html)
- [RP6502-OS Documentation](https://picocomputer.github.io/os.html)
- [WeeWX Weather Software](https://weewx.com/)
