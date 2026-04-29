# Pre-built firmware binaries

Built with ESP-IDF v5.5.3 for ESP32-C6 (Waveshare ESP32-C6-Touch-LCD-1.47).

## Files

- `bootloader.bin` — flash to offset 0x0
- `partition-table.bin` — flash to offset 0x8000
- `claude-usage-dashboard.bin` — flash to offset 0x10000

## Flash with esptool

```bash
python -m esptool --chip esp32c6 -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
    0x0 firmware/bootloader.bin \
    0x8000 firmware/partition-table.bin \
    0x10000 firmware/claude-usage-dashboard.bin
```

## Or with idf.py (after `idf.py build`)

```bash
idf.py -p /dev/ttyACM0 flash
```
