# Adding the RSS Proxy Script as a Systemd Service

The standard and most robust way to do this on a Raspberry Pi is using **systemd**. This ensures the script starts after the network is ready, restarts automatically if it crashes, and logs output correctly.

Here is the step-by-step guide.

### 1. Verify the Script Location
I will assume your script is located at `/home/pi/rss_proxy.py`.
If it is somewhere else, adjust the paths below accordingly.

Make sure we know the full path to python:
```bash
which python3
# Usually returns /usr/bin/python3
```

### 2. Create a Service File
Create a new configuration file for your service:

```bash
sudo nano /etc/systemd/system/rss-proxy.service
```

Paste the following content into the editor:

```ini
[Unit]
Description=RSS To HTTP Proxy for Picocomputer
After=network.target

[Service]
# Replace 'pi' with your username if different
User=pi
WorkingDirectory=/home/pi

# The -u flag is critical: it forces unbuffered output so logs appear immediately
ExecStart=/usr/bin/python3 -u /home/pi/rss_proxy.py

# Restart the service automatically if it crashes
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**Save and Exit:** Press `Ctrl+O`, `Enter`, then `Ctrl+X`.

### 3. Enable and Start the Service

Tell systemd to look for the new file:
```bash
sudo systemctl daemon-reload
```

Enable it to start automatically on boot:
```bash
sudo systemctl enable rss-proxy.service
```

Start it right now (without rebooting):
```bash
sudo systemctl start rss-proxy.service
```

### 4. Verify it is working

Check the status:
```bash
sudo systemctl status rss-proxy.service
```
You should see a green dot and `Active: active (running)`.

### 5. How to View Logs
Since we configured the service to run in the background, you won't see the `print()` output on your screen anymore. To see the logs (to debug connections or see if the RP6502 is connecting), use:

```bash
# View the last 50 lines of logs (press Q to quit)
journalctl -u rss-proxy.service -n 50

# OR "Follow" the logs in real-time (like tail -f)
journalctl -u rss-proxy.service -f
```

### Summary of Commands
If you ever update the python script code, you just need to restart the service:
```bash
sudo systemctl restart rss-proxy.service
```