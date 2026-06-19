# NMEA Data Parsing — Screenless Smart Wristband (EDABK Lab)

A lightweight C program that parses raw NMEA 0183 sentences from GPS module log files and outputs structured, human-readable data to a text file.

Tested on the **LC76G GNSS module** (Quectel) as part of the Screenless Smart Wristband project at **EDABK Lab**.

---

## Features

- Parses all standard NMEA 0183 sentence types
- Handles timestamped log format: `[YYYY-MM-DD_HH:MM:SS:mmm]$SENTENCE,...`
- Converts NMEA coordinates (ddmm.mmmm) to decimal degrees
- Identifies all four GNSS constellations (GPS, GLONASS, Galileo, BeiDou)
- Passes non-NMEA lines (bootloader output, proprietary commands) through unchanged
- Writes a full parsed report + summary to an output text file

---

## Supported Sentence Types

| Sentence | Description |
|----------|-------------|
| `$xxGGA` | Primary position fix — latitude, longitude, altitude, satellites, HDOP |
| `$xxRMC` | Recommended minimum — date, time, speed, course |
| `$xxGSA` | Active satellites and DOP values (PDOP, HDOP, VDOP) |
| `$xxGSV` | Satellites in view with PRN, elevation, azimuth, and SNR per satellite |
| `$xxVTG` | Course over ground and speed (knots + km/h) |
| `$xxGLL` | Geographic position (legacy LORAN-era sentence) |
| Other    | Proprietary sentences written verbatim for reference |

The `xx` prefix identifies the constellation:
- `GP` — GPS (USA)
- `GL` — GLONASS (Russia)
- `GA` — Galileo (Europe)
- `GB` — BeiDou (China)
- `GN` — Multi-constellation combined

---

## Usage

1. Place your NMEA log file in the same directory as the compiled binary.
2. Update the `in_file` filename in `main()` if needed (default: `nmea_log data_lc76g(pb).txt`).
3. Compile and run:

```bash
gcc NMEA_data_split.c -o nmea_parser
./nmea_parser
```

4. Results are written to `nmea_parsed_output.txt`.

---

## Output Example

```
[2026-04-03_13:07:44:630] $GNGGA (Multi-constellation)
  Time (UTC)  : 06:07:45
  Latitude    : 20.921361 N
  Longitude   : 105.855592 E
  Fix quality : 1 = GPS fix
  Satellites  : 13
  HDOP        : 0.72
  Altitude    : 8.385 m
  Geoid sep.  : -20.559 m

[2026-04-03_13:07:44:630] $GPGSV -- GPS Satellites in View (message 1 of 3, total visible: 11)
  Sat #1 : PRN=194  Elev=54 deg  Azim=115 deg  SNR=-- (signal too weak / not tracked)
  Sat #2 : PRN=6    Elev=56 deg  Azim=340 deg  SNR=-- (signal too weak / not tracked)
  Sat #3 : PRN=41   Elev=54 deg  Azim=229 deg  SNR=37 dBHz
  Sat #4 : PRN=19   Elev=50 deg  Azim=13 deg   SNR=29 dBHz

====================================================
  SUMMARY
  Total NMEA sentences parsed : 847
  GGA  (position fix)         : 62
  RMC  (recommended minimum)  : 62
  GSA  (active satellites)    : 248
  GSV  (satellites in view)   : 186
  VTG  (course and speed)     : 62
  GLL  (geographic position)  : 62
  Other / proprietary         : 12
====================================================
```

---

## Project Context

This parser was developed as part of the **Screenless Smart Wristband** project at **EDABK Lab**. The wristband uses a GNSS module to acquire position data, which is then transmitted and processed. This tool is used to validate and analyze raw NMEA log output captured from the module during testing.

---

## Dependencies

Standard C library only (`stdio.h`, `string.h`, `stdlib.h`). No external dependencies.
