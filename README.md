# NMEA GPS Log Parser — Screenless Smart Wristband (EDABK Lab)

A small C program I wrote to turn raw NMEA 0183 GPS logs into a structured,
human-readable report. I built it while working on the **Screenless Smart
Wristband** project at **EDABK Lab**, where I needed a quick, dependable way to
inspect what the wristband's GNSS module was actually reporting during field
tests.

The parser is tested against logs from the **Quectel LC76G** GNSS module, but it
follows the NMEA 0183 standard, so it should work with any receiver that speaks
the same sentence format.

---

## Why I built this

The LC76G streams NMEA sentences — compact, comma-separated lines like
`$GNGGA,060746.000,2055.28,N,10551.33,E,1,13,0.72,8.385,M,...`. They're easy for
a machine to read but slow for me to parse by eye, especially when I'm scanning
hundreds of them looking for when the module got a fix or how many satellites it
was tracking.

I wanted a tool that would:

- decode every sentence type the module emits, not just the position ones,
- convert the raw `ddmm.mmmm` coordinates into decimal degrees I can paste
  straight into a map,
- never silently drop a line — even proprietary or unrecognized sentences get
  written out so I don't lose anything,
- and run anywhere with nothing but a C compiler.

So I wrote it in plain C using only the standard library.

---

## What it does

- Parses all the standard NMEA 0183 sentence types (see table below)
- Handles my timestamped log format: `[YYYY-MM-DD_HH:MM:SS:mmm]$SENTENCE,...`
- Converts NMEA coordinates (`ddmm.mmmm`) to decimal degrees, with S/W negative
- Identifies all four GNSS constellations (GPS, GLONASS, Galileo, BeiDou)
- Passes non-NMEA lines (bootloader output, proprietary commands) through unchanged
- Writes a full parsed report plus a summary count to an output text file

---

## Supported sentence types

| Sentence | What I pull out of it |
|----------|-----------------------|
| `$xxGGA` | Primary position fix — latitude, longitude, altitude, satellites, HDOP |
| `$xxRMC` | Recommended minimum — date, time, speed, course |
| `$xxGSA` | Active satellites and DOP values (PDOP, HDOP, VDOP) |
| `$xxGSV` | Satellites in view, with PRN, elevation, azimuth and SNR for each |
| `$xxVTG` | Course over ground and speed (knots + km/h) |
| `$xxGLL` | Geographic position (legacy LORAN-era sentence) |
| Other    | Proprietary sentences, written verbatim for reference |

The `xx` prefix is the talker ID and tells me which constellation a sentence
came from:

- `GP` — GPS (USA)
- `GL` — GLONASS (Russia)
- `GA` — Galileo (Europe)
- `GB` — BeiDou (China)
- `GN` — multi-constellation, combined solution

---

## Building and running

I keep the dependencies to nothing but the C standard library, so a single
`gcc` command is enough:

```bash
gcc NMEA_data_split.c -o nmea_parser
./nmea_parser
```

By default it reads `nmea_log data_lc76g(pb).txt` and writes
`nmea_parsed_output.txt`. To point it at a different log, change the `in_file`
name in `main()` and recompile. (I kept the I/O filenames as constants in
`main()` to keep the program simple; swapping them for command-line arguments is
the obvious next step if I reuse this beyond the lab.)

A sample input log and its parsed output are included in this repo so you can
see the format without needing the hardware.

---

## Example output

```
[2026-04-03_13:07:45:027] $GNGGA (Multi-constellation)
  Time (UTC)  : 06:07:46
  Latitude    : 20.921361 N
  Longitude   : 105.855595 E
  Fix quality : 1 = GPS fix
  Satellites  : 13
  HDOP        : 0.72
  Altitude    : 8.385 m
  Geoid sep.  : -20.559 m

[2026-04-03_13:07:44:622] $GPGSV -- GPS Satellites in View (message 1 of 3, total visible: 11)
  Sat #1 : PRN=194  Elev=54 deg  Azim=115 deg  SNR=-- (signal too weak / not tracked)
  Sat #2 : PRN=6    Elev=56 deg  Azim=340 deg  SNR=-- (signal too weak / not tracked)
  Sat #3 : PRN=41   Elev=54 deg  Azim=229 deg  SNR=37 dBHz
  Sat #4 : PRN=19   Elev=50 deg  Azim=13 deg   SNR=29 dBHz

====================================================
  SUMMARY
  Total NMEA sentences parsed : 1300
  GGA  (position fix)         : 89
  RMC  (recommended minimum)  : 90
  GSA  (active satellites)    : 357
  GSV  (satellites in view)   : 573
  VTG  (course and speed)     : 90
  GLL  (geographic position)  : 89
  Other / proprietary         : 12
====================================================
```

*Captured from a 131-second stationary field test of the wristband's LC76G module.*

---

## How it's put together

I split the program into one small function per responsibility so each piece
stays easy to reason about:

- `split_csv()` — breaks a sentence into fields, stopping at the `*` checksum
  marker and leaving the original string intact.
- `nmea_to_decimal()` — converts `ddmm.mmmm` coordinates to decimal degrees.
- `extract_parts()` — separates the timestamp from the sentence on each log line.
- `constellation_name()` — maps a talker ID to a readable constellation name.
- `write_GGA()` / `write_RMC()` / `write_GSA()` / `write_GSV()` / `write_VTG()` /
  `write_GLL()` — one handler per sentence type, each formatting its own section
  of the report.
- `main()` — reads the log line by line, dispatches each sentence to its handler,
  and prints a summary at the end.

I commented the source generously, including the small judgement calls (for
example, how I tell an older firmware's VDOP field apart from a newer firmware's
system-ID field in the GSA sentence).

---

## Project context

This parser is part of the **Screenless Smart Wristband** project at **EDABK
Lab**. The wristband uses a GNSS module to acquire position data, which is then
transmitted and processed elsewhere in the system. I use this tool to validate
and analyze the raw NMEA output captured from the module during testing, so I
can confirm the hardware is reporting sane positions, fix quality and satellite
counts before that data goes any further downstream.

---

## Dependencies

C standard library only (`stdio.h`, `string.h`, `stdlib.h`). No external
dependencies, no build system to set up.
