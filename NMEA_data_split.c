#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Tokenizes an NMEA sentence string by comma delimiters.
 * Stops at '*' since everything after it is a checksum, not data.
 * Returns the number of fields extracted.
 *
 * Example: "$GNGGA,123456,2055.28,N" -> fields[0]="$GNGGA", fields[1]="123456", ...
 */
int split_csv(char *line, char fields[][64], int max_fields) {
    int count = 0;
    char *ptr  = line;
    while (count < max_fields) {
        char *comma = strchr(ptr, ',');
        char *star  = strchr(ptr, '*');
        char *end   = NULL;
        if      (comma && star) end = (comma < star) ? comma : star;
        else if (comma)         end = comma;
        else if (star)          end = star;

        if (end) {
            int len = (int)(end - ptr);
            if (len >= 64) len = 63;
            strncpy(fields[count], ptr, len);
            fields[count][len] = '\0';
            count++;
            if (*end == '*') break;
            ptr = end + 1;
        } else {
            strncpy(fields[count], ptr, 63);
            fields[count][63] = '\0';
            count++;
            break;
        }
    }
    return count;
}

/*
 * Converts NMEA coordinate format (ddmm.mmmm) to decimal degrees.
 * Direction 'S' or 'W' produces a negative value.
 *
 * Example: 2055.281665 N  ->  20 + (55.281665 / 60)  =  20.921361 degrees
 */
double nmea_to_decimal(double raw, char dir) {
    int    degrees = (int)(raw / 100);
    double minutes = raw - degrees * 100;
    double decimal = degrees + minutes / 60.0;
    if (dir == 'S' || dir == 'W') decimal = -decimal;
    return decimal;
}

/*
 * Strips the timestamp prefix [YYYY-MM-DD_HH:MM:SS:mmm] from a log line
 * and writes it into the ts buffer. Returns a pointer to the '$' character
 * where the NMEA sentence begins, or NULL if no sentence is found.
 */
char *extract_parts(char *line, char *ts, int ts_size) {
    ts[0] = '\0';
    if (line[0] == '[') {
        char *close = strchr(line, ']');
        if (close) {
            int len = (int)(close - line - 1);
            if (len >= ts_size) len = ts_size - 1;
            strncpy(ts, line + 1, len);
            ts[len] = '\0';
        }
        char *dollar = strchr(line, '$');
        return dollar;
    }
    if (line[0] == '$') return line;
    return NULL;
}

/*
 * Maps the two-letter talker ID prefix of an NMEA sentence to its
 * full constellation name.
 *   GP -> GPS (USA)
 *   GL -> GLONASS (Russia)
 *   GA -> Galileo (Europe)
 *   GB -> BeiDou (China)
 *   GN -> Multi-constellation combined
 */
const char *constellation_name(const char *prefix) {
    if (strncmp(prefix, "$GP", 3) == 0) return "GPS";
    if (strncmp(prefix, "$GL", 3) == 0) return "GLONASS";
    if (strncmp(prefix, "$GA", 3) == 0) return "Galileo";
    if (strncmp(prefix, "$GB", 3) == 0) return "BeiDou";
    if (strncmp(prefix, "$GN", 3) == 0) return "Multi-constellation";
    return "Unknown";
}

/*
 * Parses a $xxGGA sentence -- the primary fix data sentence in NMEA.
 * Extracts: UTC time, latitude, longitude, fix quality, satellite count,
 * HDOP, altitude, and geoid separation.
 */
void write_GGA(char *ts, char *sentence, FILE *out) {
    char fields[20][64];
    int n = split_csv(sentence, fields, 20);
    if (n < 10) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    double lat = 0, lon = 0;
    char lat_dir = '?', lon_dir = '?';
    if (strlen(fields[2]) > 0) { lat_dir = fields[3][0]; lat = nmea_to_decimal(atof(fields[2]), lat_dir); }
    if (strlen(fields[4]) > 0) { lon_dir = fields[5][0]; lon = nmea_to_decimal(atof(fields[4]), lon_dir); }

    const char *fix_str[] = { "No fix", "GPS fix", "DGPS fix", "PPS fix",
                               "RTK fixed", "RTK float", "Estimated", "Manual", "Simulation" };
    int fix = atoi(fields[6]);
    const char *fix_label = (fix >= 0 && fix <= 8) ? fix_str[fix] : "Unknown";

    fprintf(out, "[%s] %s (%s)\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Time (UTC)  : %.2s:%.2s:%.2s\n", fields[1], fields[1]+2, fields[1]+4);
    if (strlen(fields[2]) > 0)
        fprintf(out, "  Latitude    : %.6f %c\n", lat, lat_dir);
    else
        fprintf(out, "  Latitude    : (no fix)\n");
    if (strlen(fields[4]) > 0)
        fprintf(out, "  Longitude   : %.6f %c\n", lon, lon_dir);
    else
        fprintf(out, "  Longitude   : (no fix)\n");
    fprintf(out, "  Fix quality : %d = %s\n", fix, fix_label);
    fprintf(out, "  Satellites  : %s\n", fields[7]);
    fprintf(out, "  HDOP        : %s\n", fields[8]);
    if (strlen(fields[9]) > 0)
        fprintf(out, "  Altitude    : %s m\n", fields[9]);
    /* Geoid separation: difference between the WGS84 ellipsoid and mean sea level */
    if (n > 11 && strlen(fields[11]) > 0)
        fprintf(out, "  Geoid sep.  : %s m\n", fields[11]);
    fprintf(out, "\n");
}

/*
 * Parses a $xxRMC sentence -- Recommended Minimum Navigation Information.
 * The only standard NMEA sentence that carries date alongside time.
 * Extracts: status, date, UTC time, latitude, longitude, speed, and course.
 */
void write_RMC(char *ts, char *sentence, FILE *out) {
    char fields[15][64];
    int n = split_csv(sentence, fields, 15);
    if (n < 8) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    /* Status: A = Active (valid fix), V = Void (no fix) */
    char status = (strlen(fields[2]) > 0) ? fields[2][0] : '?';

    fprintf(out, "[%s] %s (%s)\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Status      : %c (%s)\n", status, status == 'A' ? "Active / Valid fix" : "Void / No fix");
    if (n > 9 && strlen(fields[9]) > 0)
        fprintf(out, "  Date        : %.2s/%.2s/%.2s\n", fields[9], fields[9]+2, fields[9]+4);
    fprintf(out, "  Time (UTC)  : %.2s:%.2s:%.2s\n", fields[1], fields[1]+2, fields[1]+4);
    if (status == 'A') {
        double lat = nmea_to_decimal(atof(fields[3]), fields[4][0]);
        double lon = nmea_to_decimal(atof(fields[5]), fields[6][0]);
        fprintf(out, "  Latitude    : %.6f %c\n", lat, fields[4][0]);
        fprintf(out, "  Longitude   : %.6f %c\n", lon, fields[6][0]);
        /* 1 knot = 1.852 km/h */
        fprintf(out, "  Speed       : %s knots  (%.2f km/h)\n", fields[7], atof(fields[7]) * 1.852);
        fprintf(out, "  Course      : %s deg\n", fields[8]);
    }
    fprintf(out, "\n");
}

/*
 * Parses a $xxGSA sentence -- GNSS DOP and Active Satellites.
 * Each constellation (GPS, GLONASS, Galileo, BeiDou) sends its own GSA,
 * so up to four consecutive GSA sentences are expected per epoch.
 * Extracts: fix mode, active satellite PRNs, PDOP, HDOP, VDOP, and system ID.
 */
void write_GSA(char *ts, char *sentence, FILE *out) {
    char fields[22][64];
    int n = split_csv(sentence, fields, 22);
    if (n < 17) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    const char *mode2_str[] = { "?", "No fix", "2D fix", "3D fix" };
    int mode2 = atoi(fields[2]);
    const char *mode2_label = (mode2 >= 1 && mode2 <= 3) ? mode2_str[mode2] : "?";

    /* System ID occupies the last field (index 17) before the checksum */
    const char *sys_name = "Unknown";
    if (n >= 18) {
        int sys = atoi(fields[17]);
        if      (sys == 1) sys_name = "GPS";
        else if (sys == 2) sys_name = "GLONASS";
        else if (sys == 3) sys_name = "Galileo";
        else if (sys == 4) sys_name = "BeiDou";
    }

    fprintf(out, "[%s] %s -- Active Satellites (%s / %s)\n", ts, fields[0], sys_name, mode2_label);
    fprintf(out, "  Selection   : %s\n", fields[1][0] == 'A' ? "Automatic" : "Manual");

    /* Active satellite PRNs are listed in fields[3] through fields[14] (up to 12 satellites) */
    fprintf(out, "  Active PRNs : ");
    int any = 0;
    for (int i = 3; i <= 14 && i < n; i++) {
        if (strlen(fields[i]) > 0 && atoi(fields[i]) > 0) {
            fprintf(out, "%s ", fields[i]);
            any = 1;
        }
    }
    if (!any) fprintf(out, "(none)");
    fprintf(out, "\n");

    if (strlen(fields[15]) > 0) fprintf(out, "  PDOP        : %s\n", fields[15]);
    if (strlen(fields[16]) > 0) fprintf(out, "  HDOP        : %s\n", fields[16]);
    /* VDOP is in field[17] only when System ID is absent (older firmware) */
    if (n >= 17 && strlen(fields[17]) > 0 && atoi(fields[17]) > 4)
        fprintf(out, "  VDOP        : %s\n", fields[17]);
    fprintf(out, "\n");
}

/*
 * Parses a $xxGSV sentence -- GNSS Satellites in View.
 * Each message carries up to 4 satellites; multiple messages are sent per epoch.
 * This handler is shared across all constellations (GP/GL/GA/GB).
 * Extracts: PRN, elevation, azimuth, and SNR (signal strength in dBHz).
 */
void write_GSV(char *ts, char *sentence, FILE *out) {
    char fields[24][64];
    int n = split_csv(sentence, fields, 24);
    if (n < 4) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    int total_msgs = atoi(fields[1]);
    int msg_num    = atoi(fields[2]);
    int total_sats = atoi(fields[3]);
    const char *sys = constellation_name(fields[0]);

    fprintf(out, "[%s] %s -- %s Satellites in View (message %d of %d, total visible: %d)\n",
            ts, fields[0], sys, msg_num, total_msgs, total_sats);

    /* Each satellite block is 4 fields: PRN, elevation, azimuth, SNR */
    int i = 4;
    int sat_num = (msg_num - 1) * 4 + 1;
    while (i < n && i + 2 < 24) {
        if (strlen(fields[i]) == 0) break;
        int prn  = atoi(fields[i]);
        int elev = (i + 1 < n) ? atoi(fields[i + 1]) : 0;
        int azim = (i + 2 < n) ? atoi(fields[i + 2]) : 0;
        int snr  = (i + 3 < n && strlen(fields[i + 3]) > 0) ? atoi(fields[i + 3]) : -1;

        fprintf(out, "  Sat #%-2d : PRN=%-3d  Elev=%2d deg  Azim=%3d deg  SNR=",
                sat_num++, prn, elev, azim);
        if (snr >= 0) fprintf(out, "%d dBHz\n", snr);
        else          fprintf(out, "-- (signal too weak / not tracked)\n");
        i += 4;
    }
    fprintf(out, "\n");
}

/*
 * Parses a $xxVTG sentence -- Course Over Ground and Ground Speed.
 * Provides speed in both knots and km/h, and true/magnetic heading.
 * Note: speed data also exists in RMC; VTG adds the km/h field explicitly.
 */
void write_VTG(char *ts, char *sentence, FILE *out) {
    char fields[12][64];
    int n = split_csv(sentence, fields, 12);
    if (n < 8) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    fprintf(out, "[%s] %s (%s) -- Course and Speed\n", ts, fields[0], constellation_name(fields[0]));
    if (strlen(fields[1]) > 0) fprintf(out, "  Course (True)     : %s deg\n", fields[1]);
    if (strlen(fields[3]) > 0) fprintf(out, "  Course (Magnetic) : %s deg\n", fields[3]);
    if (strlen(fields[5]) > 0) fprintf(out, "  Speed             : %s knots  (%.2f km/h)\n",
                                        fields[5], atof(fields[5]) * 1.852);
    if (strlen(fields[7]) > 0) fprintf(out, "  Speed (km/h)      : %s km/h\n", fields[7]);
    fprintf(out, "\n");
}

/*
 * Parses a $xxGLL sentence -- Geographic Position Latitude/Longitude.
 * A legacy sentence inherited from LORAN navigation systems.
 * Contains less information than GGA; included here for completeness.
 */
void write_GLL(char *ts, char *sentence, FILE *out) {
    char fields[10][64];
    int n = split_csv(sentence, fields, 10);
    if (n < 6) { fprintf(out, "[%s] %s | (incomplete sentence, skipped)\n\n", ts, fields[0]); return; }

    char status = (strlen(fields[6]) > 0) ? fields[6][0] : '?';
    fprintf(out, "[%s] %s (%s) -- Geographic Position\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Status    : %c (%s)\n", status, status == 'A' ? "Valid" : "Void");
    if (status == 'A' && strlen(fields[1]) > 0) {
        double lat = nmea_to_decimal(atof(fields[1]), fields[2][0]);
        double lon = nmea_to_decimal(atof(fields[3]), fields[4][0]);
        fprintf(out, "  Latitude  : %.6f %c\n", lat, fields[2][0]);
        fprintf(out, "  Longitude : %.6f %c\n", lon, fields[4][0]);
        fprintf(out, "  Time      : %.2s:%.2s:%.2s\n", fields[5], fields[5]+2, fields[5]+4);
    }
    fprintf(out, "\n");
}

/*
 * Writes unrecognized sentences (proprietary $PAIR, $PQTM, bootloader output,
 * etc.) verbatim to the output file for reference.
 */
void write_unknown(char *ts, char *sentence, FILE *out) {
    fprintf(out, "[%s] (proprietary / unrecognized) %s\n\n", ts, sentence);
}

int main(void) {
    const char *in_file  = "nmea_log data_lc76g(pb).txt";
    const char *out_file = "nmea_parsed_output.txt";

    FILE *fp  = fopen(in_file,  "r");
    FILE *out = fopen(out_file, "w");

    if (!fp)  { printf("Error: cannot open input file: %s\n",  in_file);  return 1; }
    if (!out) { printf("Error: cannot open output file: %s\n", out_file); fclose(fp); return 1; }

    fprintf(out, "====================================================\n");
    fprintf(out, "  NMEA GPS Log -- Parsed Output\n");
    fprintf(out, "  Source : %s\n", in_file);
    fprintf(out, "====================================================\n\n");

    char line[512];
    int total = 0, gga = 0, rmc = 0, gsa = 0, gsv = 0, vtg = 0, gll = 0, other = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        char ts[40] = "";
        char *nmea = extract_parts(line, ts, sizeof(ts));

        /* Pass non-NMEA lines (bootloader output, comments) through unchanged */
        if (!nmea) {
            if (strlen(line) > 0)
                fprintf(out, "[%s] (non-NMEA) %s\n\n", ts, line);
            continue;
        }

        total++;

        /* Skip the two-letter talker ID ($GN, $GP, etc.) to isolate sentence type */
        char *type = nmea + 3;

        if      (strncmp(type, "GGA", 3) == 0) { write_GGA(ts, nmea, out); gga++; }
        else if (strncmp(type, "RMC", 3) == 0) { write_RMC(ts, nmea, out); rmc++; }
        else if (strncmp(type, "GSA", 3) == 0) { write_GSA(ts, nmea, out); gsa++; }
        else if (strncmp(type, "GSV", 3) == 0) { write_GSV(ts, nmea, out); gsv++; }
        else if (strncmp(type, "VTG", 3) == 0) { write_VTG(ts, nmea, out); vtg++; }
        else if (strncmp(type, "GLL", 3) == 0) { write_GLL(ts, nmea, out); gll++; }
        else                                    { write_unknown(ts, nmea, out); other++; }
    }

    fprintf(out, "====================================================\n");
    fprintf(out, "  SUMMARY\n");
    fprintf(out, "  Total NMEA sentences parsed : %d\n", total);
    fprintf(out, "  GGA  (position fix)         : %d\n", gga);
    fprintf(out, "  RMC  (recommended minimum)  : %d\n", rmc);
    fprintf(out, "  GSA  (active satellites)    : %d\n", gsa);
    fprintf(out, "  GSV  (satellites in view)   : %d\n", gsv);
    fprintf(out, "  VTG  (course and speed)     : %d\n", vtg);
    fprintf(out, "  GLL  (geographic position)  : %d\n", gll);
    fprintf(out, "  Other / proprietary         : %d\n", other);
    fprintf(out, "====================================================\n");

    fclose(fp);
    fclose(out);

    printf("Done. Parsed output written to: %s\n", out_file);
    printf("Total NMEA sentences processed: %d\n", total);
    return 0;
}
