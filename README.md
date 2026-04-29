# Claude Usage Dashboard — Waveshare ESP32-C6-Touch-LCD-1.47

Compact desk gadget that shows Claude API usage limits (5h + 7d) for one or
more accounts and rotates between them on a 320x172 dark-theme display.

## Hardware

- Waveshare ESP32-C6-Touch-LCD-1.47 (ESP32-C6FH4, 8 MB flash, 1.47" 172x320 IPS LCD)
- Display: JD9853, Touch: AXS5106L (I2C)

## Build

```bash
. ~/.espressif/v5.5.3/esp-idf/export.sh
cd ~/claude-dashboard-work/claude-usage-dashboard
idf.py set-target esp32c6
idf.py build
```

## Flash

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

The board enumerates as native USB CDC (no serial chip). If the port name
differs, check `dmesg | grep tty` after plugging it in.

## First boot

1. Power on. Display shows "Setup mode" with an open SSID `Claude-Dashboard-XXXX`.
2. Connect to that SSID from your phone or laptop. A captive portal opens (or
   visit `http://192.168.4.1`). Submit your home WiFi credentials.
3. Device reboots, joins your WiFi, and announces itself as
   `claude-dashboard.local` on mDNS. The display now reads "No accounts —
   open claude-dashboard.local".
4. Open `http://claude-dashboard.local` (or the device's IP from your router)
   to add an account.

## Adding an account

You can authenticate each account with either an OAuth token pair (recommended
for Claude.ai accounts using `claude` CLI) or a raw API key.

### OAuth (claude CLI)

The Claude CLI keeps tokens in `~/.claude/.credentials.json`. Extract them:

```bash
python3 -c "import json; d = json.load(open('$HOME/.claude/.credentials.json'))['claudeAiOauth']; print('access:', d['accessToken']); print('refresh:', d['refreshToken']); print('expiresAt:', d['expiresAt'])"
```

Paste those values into the **OAuth** tab of the admin page. The device
will refresh the access token automatically before it expires.

### API key

Sign in to <https://console.anthropic.com/settings/keys>, create a key, and
paste it in the **API key** tab. (Note: API keys cannot read the same
`/api/oauth/usage` endpoint — only OAuth-authenticated tokens can.
The dashboard tries the same endpoint with the API key, but if that returns
401 the display shows the error. For organization usage, OAuth is the
practical path.)

## Touch

- **Tap** — next account
- **Long press 3 s** — factory reset (clears WiFi + all accounts)
  Hold prompt appears after 0.5 s; release before 3 s to cancel.

## Auto-cycle

If you have multiple accounts and the cycle setting is enabled (default 30 s),
the display rotates through them automatically. A tap pauses cycling for 60 s.

## Sleep schedule

Configurable in admin → Sleep schedule. Default 23:00 → 07:00. Backlight goes
to 0 % during the window. Touch wakes the display for 15 minutes.

## Layout

```
main/
  app_main.c           init + poll loop
  app_config.[ch]      NVS — WiFi, accounts, settings
  app_wifi.[ch]        STA + AP
  app_portal.[ch]      Captive portal (AP mode)
  app_admin.[ch]       Admin web server (STA mode)
  app_claude_api.[ch]  HTTPS client + OAuth refresh
  app_display.[ch]     LVGL setup
  app_touch.[ch]       Tap + long-press detection
  app_sleep.[ch]       Backlight schedule
  ui_dashboard.[ch]    Main dashboard screen
  ui_setup.[ch]        AP-mode info screen

components/
  esp_bsp/             Waveshare BSP (display, touch, i2c, spi, wifi, ...)
  esp_lcd_jd9853/      JD9853 panel driver
  esp_lcd_touch_axs5106/ AXS5106L touch driver
```

## License

MIT (firmware code). Vendor BSP and panel/touch drivers retain their original
Waveshare/Espressif licenses.
