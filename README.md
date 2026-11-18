# WxWall — custom firmware for the FlightWall Mini

Replacement firmware for the [FlightWall Mini](https://theflightwall.com) LED display that shows
nearby flights (with airline logos and routes), METARs, a TAF timeline and a multi-day forecast,
all configured from a web page on your network.

The Mini is a Huidu **HD-WF2** controller (ESP32-S3, 8 MB flash, no PSRAM) driving a
**128×64 P2.5 HUB75** panel (320×160 mm, 1/32 scan) on its `75EX1` connector.

## Pages

| Page | Source |
|---|---|
| Nearby flights — logo, flight number, airline, type/registration, route, altitude/trend, speed, distance | Positions from your **PiAware** (`aircraft.json`) or [adsb.lol](https://adsb.lol); routes from [adsbdb](https://www.adsbdb.com), falling back to **FlightAware AeroAPI** |
| METAR — one page per airport in your list | [Tachbase](https://www.tachbase.com) API |
| TAF timeline — 24 h of flight categories for the first airport | Tachbase API |
| Forecast — 4 days for a city/ZIP | [Open-Meteo](https://open-meteo.com) |

Settings live at **http://wxwall.local** (or the IP shown on the panel after Wi-Fi setup).

### AeroAPI budget

AeroAPI is only used when adsbdb has no plausible route for an airline callsign. Usage is counted on
the device (persisted across restarts), paced across the month, and capped by the **lookups per month**
setting (default 1,800 — under the 2,000 that the $10 ADS-B feeder credit covers at $0.005 each).
Calls are spaced at least 8 s apart to stay under the Personal tier's 10/min limit.

## Build

Requires Python 3 and [PlatformIO](https://platformio.org).

```sh
python3 -m venv .venv
.venv/bin/pip install platformio esptool pillow
cd wxwall
../.venv/bin/python -I tools/make_assets.py   # generates src/assets_logos.h and src/assets_airlines.h
../.venv/bin/pio run
```

`make_assets.py` downloads airline logos (from
[Jxck-S/airline-logos](https://github.com/Jxck-S/airline-logos) and
[sexym0nk3y/airline-logos](https://github.com/sexym0nk3y/airline-logos)) and the
[OpenFlights](https://openflights.org/data) airline list (ODbL), then picks the most legible logo
per airline at 32×32. Logos are trademarks of their airlines; the generated files are not committed.

## Flash

**First time (USB):** connect the board's USB-A port to your computer with a USB-A↔USB-A cable
(a USB-C↔USB-A cable straight into a Mac does not work). Unplug the display's own power while
USB-powered.

```sh
../.venv/bin/esptool --port /dev/cu.usbmodem* read-flash 0 0x800000 stock-backup.bin   # back up first!
../.venv/bin/pio run -t upload
```

On first boot the panel shows **WIFI SETUP** — join the `WxWall-Setup` network and pick your Wi-Fi.

**Updates (Wi-Fi):** upload `wxwall/.pio/build/hd-wf2/firmware.bin` from the web page, or

```sh
curl -F firmware=@.pio/build/hd-wf2/firmware.bin http://wxwall.local/update
```

## Restore the stock FlightWall firmware

```sh
../.venv/bin/esptool --port /dev/cu.usbmodem* write-flash 0 stock-backup.bin
```

Your backup contains your Wi-Fi credentials and the device's FlightWall provisioning — keep it private.

## Hardware notes

- Pin map: [`wxwall/src/board.h`](wxwall/src/board.h) (from
  [mrcodetastic/HD-WF1-WF2-LED-MatrixPanel-DMA](https://github.com/mrcodetastic/HD-WF1-LED-MatrixPanel-DMA)).
- Button `S1` (GPIO 17): short press = next page, hold 2 s = refresh data.
- Display driver: [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) 3.0.14.
