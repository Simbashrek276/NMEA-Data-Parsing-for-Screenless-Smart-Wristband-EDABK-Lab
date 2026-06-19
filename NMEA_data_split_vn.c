#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * hàm này cắt một câu NMEA thành từng mảnh nhỏ theo dấu phẩy de phan tich them
 * Gặp dấu '*' thì dừng lại vì phần sau dấu '*' là checksum, không phải dữ liệu.
 * Trả về số lượng field cắt được.
 *
 * Ví dụ: "$GNGGA,123456,2055.28,N" → fields[0]="$GNGGA", fields[1]="123456", ...
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
            /* hết chuỗi rồi, lấy nốt phần còn lại */
            strncpy(fields[count], ptr, 63);
            fields[count][63] = '\0';
            count++;
            break;
        }
    }
    return count;
}

/*
 * Chuyển tọa độ từ định dạng NMEA (ddmm.mmmm) sang độ thập phân.
 * Nếu hướng là 'S' (Nam) hoặc 'W' (Tây) thì kết quả sẽ là số âm.
 *
 * Ví dụ: 2055.281665 N → 20 + (55.281665 / 60) = 20.921361 độ
 */
double nmea_to_decimal(double raw, char dir) {
    int    degrees = (int)(raw / 100);
    double minutes = raw - degrees * 100;
    double decimal = degrees + minutes / 60.0;
    if (dir == 'S' || dir == 'W') decimal = -decimal;
    return decimal;
}

/*
 * Tách phần timestamp [YYYY-MM-DD_HH:MM:SS:mmm] ra khỏi dòng log,
 * lưu vào buffer ts, rồi trả về con trỏ tới ký tự '$' — tức là chỗ
 * câu NMEA bắt đầu. Nếu không tìm thấy '$' thì trả về NULL.
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
 * Nhìn vào 3 ký tự đầu của câu NMEA để xác định hệ thống vệ tinh nào đang nói:
 *   $GP → GPS (Mỹ)
 *   $GL → GLONASS (Nga)
 *   $GA → Galileo (châu Âu)
 *   $GB → BeiDou (Trung Quốc)
 *   $GN → Nhiều hệ thống kết hợp (multi-constellation)
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
 * Xử lý câu $xxGGA — câu quan trọng nhất trong NMEA, chứa thông tin vị trí chính.
 * Lấy ra: giờ UTC, vĩ độ, kinh độ, chất lượng fix, số vệ tinh, HDOP, độ cao, và geoid.
 */
void write_GGA(char *ts, char *sentence, FILE *out) {
    char fields[20][64];
    int n = split_csv(sentence, fields, 20);
    if (n < 10) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    double lat = 0, lon = 0;
    char lat_dir = '?', lon_dir = '?';
    if (strlen(fields[2]) > 0) { lat_dir = fields[3][0]; lat = nmea_to_decimal(atof(fields[2]), lat_dir); }
    if (strlen(fields[4]) > 0) { lon_dir = fields[5][0]; lon = nmea_to_decimal(atof(fields[4]), lon_dir); }

    /* Fix quality từ 0 đến 8, mỗi số có ý nghĩa khác nhau */
    const char *fix_str[] = { "No fix", "GPS fix", "DGPS fix", "PPS fix",
                               "RTK fixed", "RTK float", "Estimated", "Manual", "Simulation" };
    int fix = atoi(fields[6]);
    const char *fix_label = (fix >= 0 && fix <= 8) ? fix_str[fix] : "Unknown";

    fprintf(out, "[%s] %s (%s)\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Time (UTC)  : %.2s:%.2s:%.2s\n", fields[1], fields[1]+2, fields[1]+4);
    if (strlen(fields[2]) > 0)
        fprintf(out, "  Latitude    : %.6f %c\n", lat, lat_dir);
    else
        fprintf(out, "  Latitude    : (chưa có fix)\n");
    if (strlen(fields[4]) > 0)
        fprintf(out, "  Longitude   : %.6f %c\n", lon, lon_dir);
    else
        fprintf(out, "  Longitude   : (chưa có fix)\n");
    fprintf(out, "  Fix quality : %d = %s\n", fix, fix_label);
    fprintf(out, "  Satellites  : %s\n", fields[7]);
    fprintf(out, "  HDOP        : %s\n", fields[8]);
    if (strlen(fields[9]) > 0)
        fprintf(out, "  Altitude    : %s m\n", fields[9]);
    /* Geoid separation: chênh lệch giữa mặt ellipsoid WGS84 và mực nước biển thực tế */
    if (n > 11 && strlen(fields[11]) > 0)
        fprintf(out, "  Geoid sep.  : %s m\n", fields[11]);
    fprintf(out, "\n");
}

/*
 * Xử lý câu $xxRMC — "Recommended Minimum", câu bắt buộc theo chuẩn NMEA.
 * Đây là câu duy nhất chứa cả NGÀY THÁNG lẫn tốc độ và hướng di chuyển.
 */
void write_RMC(char *ts, char *sentence, FILE *out) {
    char fields[15][64];
    int n = split_csv(sentence, fields, 15);
    if (n < 8) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    /* Status: A = Active (có fix), V = Void (không có fix) */
    char status = (strlen(fields[2]) > 0) ? fields[2][0] : '?';

    fprintf(out, "[%s] %s (%s)\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Status      : %c (%s)\n", status, status == 'A' ? "Active / có tín hiệu" : "Void / không có fix");
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
 * Xử lý câu $xxGSA — cho biết vệ tinh nào đang thực sự tham gia tính vị trí,
 * và các chỉ số DOP (độ chính xác hình học: PDOP, HDOP, VDOP).
 * Mỗi hệ thống GPS/GLONASS/Galileo/BeiDou gửi một câu GSA riêng,
 * nên thường có tới 4 câu GSA liên tiếp trong một chu kỳ.
 */
void write_GSA(char *ts, char *sentence, FILE *out) {
    char fields[22][64];
    int n = split_csv(sentence, fields, 22);
    if (n < 17) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    const char *mode2_str[] = { "?", "No fix", "2D fix", "3D fix" };
    int mode2 = atoi(fields[2]);
    const char *mode2_label = (mode2 >= 1 && mode2 <= 3) ? mode2_str[mode2] : "?";

    /* Field cuối cùng (index 17) là System ID — cho biết câu này thuộc hệ thống nào */
    const char *sys_name = "Unknown";
    if (n >= 18) {
        int sys = atoi(fields[17]);
        if      (sys == 1) sys_name = "GPS";
        else if (sys == 2) sys_name = "GLONASS";
        else if (sys == 3) sys_name = "Galileo";
        else if (sys == 4) sys_name = "BeiDou";
    }

    fprintf(out, "[%s] %s -- Vệ tinh đang dùng (%s / %s)\n", ts, fields[0], sys_name, mode2_label);
    fprintf(out, "  Selection   : %s\n", fields[1][0] == 'A' ? "Tự động" : "Thủ công");

    /* Danh sách PRN các vệ tinh đang tham gia tính vị trí, từ field[3] đến field[14] (tối đa 12 vệ tinh) */
    fprintf(out, "  Active PRNs : ");
    int any = 0;
    for (int i = 3; i <= 14 && i < n; i++) {
        if (strlen(fields[i]) > 0 && atoi(fields[i]) > 0) {
            fprintf(out, "%s ", fields[i]);
            any = 1;
        }
    }
    if (!any) fprintf(out, "(không có)");
    fprintf(out, "\n");

    if (strlen(fields[15]) > 0) fprintf(out, "  PDOP        : %s\n", fields[15]);
    if (strlen(fields[16]) > 0) fprintf(out, "  HDOP        : %s\n", fields[16]);
    /* VDOP nằm ở field[17] chỉ khi không có System ID (firmware cũ hơn) */
    if (n >= 17 && strlen(fields[17]) > 0 && atoi(fields[17]) > 4)
        fprintf(out, "  VDOP        : %s\n", fields[17]);
    fprintf(out, "\n");
}

/*
 * Xử lý câu $xxGSV - danh sách tất cả vệ tinh nhìn thấy được (không chỉ những vệ tinh đang dùng).
 * Mỗi câu chứa tối đa 4 vệ tinh, nên thường có nhiều câu GSV liên tiếp.
 * Dùng chung cho tất cả hệ thống: GP, GL, GA, GB.
 * Mỗi vệ tinh có: PRN, góc ngẩng (elevation), góc phương vị (azimuth), và cường độ tín hiệu (SNR).
 */
void write_GSV(char *ts, char *sentence, FILE *out) {
    char fields[24][64];
    int n = split_csv(sentence, fields, 24);
    if (n < 4) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    int total_msgs = atoi(fields[1]);
    int msg_num    = atoi(fields[2]);
    int total_sats = atoi(fields[3]);
    const char *sys = constellation_name(fields[0]);

    fprintf(out, "[%s] %s -- Vệ tinh %s trong tầm nhìn (tin %d/%d, tổng cộng: %d vệ tinh)\n",
            ts, fields[0], sys, msg_num, total_msgs, total_sats);

    /* Mỗi vệ tinh chiếm 4 field liên tiếp: PRN, elevation, azimuth, SNR */
    int i = 4;
    int sat_num = (msg_num - 1) * 4 + 1;
    while (i < n && i + 2 < 24) {
        if (strlen(fields[i]) == 0) break;
        int prn  = atoi(fields[i]);
        int elev = (i + 1 < n) ? atoi(fields[i + 1]) : 0;
        int azim = (i + 2 < n) ? atoi(fields[i + 2]) : 0;
        int snr  = (i + 3 < n && strlen(fields[i + 3]) > 0) ? atoi(fields[i + 3]) : -1;

        fprintf(out, "  Vệ tinh #%-2d : PRN=%-3d  Ngẩng=%2d°  Phương vị=%3d°  SNR=",
                sat_num++, prn, elev, azim);
        if (snr >= 0) fprintf(out, "%d dBHz\n", snr);
        else          fprintf(out, "-- (tín hiệu yếu / chưa bắt được)\n");
        i += 4;
    }
    fprintf(out, "\n");
}

/*
 * Xử lý câu $xxVTG — hướng di chuyển và tốc độ so với mặt đất.
 * Cho tốc độ theo cả hai đơn vị: knots và km/h, cùng với hướng thực và hướng từ.
 * Lưu ý: tốc độ cũng có trong RMC, nhưng VTG tiện hơn vì có sẵn km/h.
 */
void write_VTG(char *ts, char *sentence, FILE *out) {
    char fields[12][64];
    int n = split_csv(sentence, fields, 12);
    if (n < 8) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    fprintf(out, "[%s] %s (%s) -- Hướng và tốc độ di chuyển\n", ts, fields[0], constellation_name(fields[0]));
    if (strlen(fields[1]) > 0) fprintf(out, "  Hướng thực (True)   : %s độ\n", fields[1]);
    if (strlen(fields[3]) > 0) fprintf(out, "  Hướng từ (Magnetic) : %s độ\n", fields[3]);
    if (strlen(fields[5]) > 0) fprintf(out, "  Tốc độ              : %s knots  (%.2f km/h)\n",
                                        fields[5], atof(fields[5]) * 1.852);
    if (strlen(fields[7]) > 0) fprintf(out, "  Tốc độ (km/h)       : %s km/h\n", fields[7]);
    fprintf(out, "\n");
}

/*
 * Xử lý câu $xxGLL — tọa độ kinh vĩ độ đơn giản.
 * Đây là câu cũ, kế thừa từ hệ thống hàng hải LORAN.
 * Chứa ít thông tin hơn GGA, nhưng vẫn được giữ lại cho đầy đủ.
 */
void write_GLL(char *ts, char *sentence, FILE *out) {
    char fields[10][64];
    int n = split_csv(sentence, fields, 10);
    if (n < 6) { fprintf(out, "[%s] %s | (câu không đủ field, bỏ qua)\n\n", ts, fields[0]); return; }

    char status = (strlen(fields[6]) > 0) ? fields[6][0] : '?';
    fprintf(out, "[%s] %s (%s) -- Tọa độ địa lý\n", ts, fields[0], constellation_name(fields[0]));
    fprintf(out, "  Trạng thái : %c (%s)\n", status, status == 'A' ? "Hợp lệ" : "Không hợp lệ");
    if (status == 'A' && strlen(fields[1]) > 0) {
        double lat = nmea_to_decimal(atof(fields[1]), fields[2][0]);
        double lon = nmea_to_decimal(atof(fields[3]), fields[4][0]);
        fprintf(out, "  Vĩ độ     : %.6f %c\n", lat, fields[2][0]);
        fprintf(out, "  Kinh độ   : %.6f %c\n", lon, fields[4][0]);
        fprintf(out, "  Giờ       : %.2s:%.2s:%.2s\n", fields[5], fields[5]+2, fields[5]+4);
    }
    fprintf(out, "\n");
}

/*
 * Với những câu không nhận ra được (lệnh riêng của nhà sản xuất như $PAIR, $PQTM,
 * hoặc output bootloader...), cứ ghi nguyên văn vào file output để tham khảo sau.
 */
void write_unknown(char *ts, char *sentence, FILE *out) {
    fprintf(out, "[%s] (không nhận dạng được / proprietary) %s\n\n", ts, sentence);
}

int main(void) {
    const char *in_file  = "nmea_log data_lc76g(pb).txt"; /*a dien ten file vao day nhe*/
    const char *out_file = "nmea_parsed_output.txt";

    FILE *fp  = fopen(in_file,  "r");
    FILE *out = fopen(out_file, "w");

    if (!fp)  { printf("Lỗi: không mở được file đầu vào: %s\n",  in_file);  return 1; }
    if (!out) { printf("Lỗi: không mở được file đầu ra: %s\n", out_file); fclose(fp); return 1; }

    fprintf(out, "====================================================\n");
    fprintf(out, "  NMEA GPS Log -- Ket qua phan tich\n");
    fprintf(out, "  Nguon : %s\n", in_file);
    fprintf(out, "====================================================\n\n");

    char line[512];
    int total = 0, gga = 0, rmc = 0, gsa = 0, gsv = 0, vtg = 0, gll = 0, other = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        char ts[40] = "";
        char *nmea = extract_parts(line, ts, sizeof(ts));

        /* Dòng không phải NMEA (bootloader output, ghi chú...) thì ghi nguyên vào file */
        if (!nmea) {
            if (strlen(line) > 0)
                fprintf(out, "[%s] (khong phai NMEA) %s\n\n", ts, line);
            continue;
        }

        total++;

        /* Bỏ qua 3 ký tự đầu ($GN, $GP...) để lấy loại câu: GGA, RMC, GSA... */
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
    fprintf(out, "  TONG KET\n");
    fprintf(out, "  Tong so cau NMEA da xu ly    : %d\n", total);
    fprintf(out, "  GGA  (vi tri chinh)           : %d\n", gga);
    fprintf(out, "  RMC  (thong tin toi thieu)    : %d\n", rmc);
    fprintf(out, "  GSA  (ve tinh dang dung)      : %d\n", gsa);
    fprintf(out, "  GSV  (ve tinh trong tam nhin) : %d\n", gsv);
    fprintf(out, "  VTG  (huong va toc do)        : %d\n", vtg);
    fprintf(out, "  GLL  (toa do dia ly)          : %d\n", gll);
    fprintf(out, "  Khac / proprietary            : %d\n", other);
    fprintf(out, "====================================================\n");

    fclose(fp);
    fclose(out);

    printf("Xong! Ket qua da duoc ghi vao file: %s\n", out_file);
    printf("Tong so cau NMEA da xu ly: %d\n", total);
    return 0;
}
