/* ============================================================
 * 文件: src/transport/gh_transport.c
 * 功能: 通信层实现（POSIX串口 + 分帧逻辑）
 * 说明: 使用 termios/select/pthread，替换 QSerialPort + QTimer
 *       分帧核心逻辑迁移自 mainwindow.cpp::handleSerialReadyRead
 * ============================================================ */

#include "gh_transport.h"
#include "../port/gh_os_port.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <termios.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
  #include <sys/time.h>
#ifdef __APPLE__
  #include <IOKit/serial/ioss.h>
#endif
#endif



#ifdef _WIN32
/* ============================================================
 * 内部辅助：设置串口属性（Windows）
 * ============================================================ */
static bool s_configure_serial(HANDLE hComm, const gh_serial_config_t* cfg) {
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(hComm, &dcb)) {
        return false;
    }

    dcb.BaudRate = cfg->baud_rate;
    switch (cfg->data_bits) {
        case 5: dcb.ByteSize = 5; break;
        case 6: dcb.ByteSize = 6; break;
        case 7: dcb.ByteSize = 7; break;
        default: dcb.ByteSize = 8; break;
    }

    if (cfg->parity == 'E') {
        dcb.Parity = EVENPARITY;
        dcb.fParity = TRUE;
    } else if (cfg->parity == 'O') {
        dcb.Parity = ODDPARITY;
        dcb.fParity = TRUE;
    } else {
        dcb.Parity = NOPARITY;
        dcb.fParity = FALSE;
    }

    if (cfg->stop_bits == 2) {
        dcb.StopBits = TWOSTOPBITS;
    } else {
        dcb.StopBits = ONESTOPBIT;
    }

    if (cfg->flow_control) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else {
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
    }

    if (!SetCommState(hComm, &dcb)) {
        return false;
    }

    /* 放大驱动层收发缓冲，降低 BLE 透传突发数据导致的丢字节概率 */
    SetupComm(hComm, 64 * 1024, 64 * 1024);
    PurgeComm(hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* 配置超时，使其像非阻塞 read + select 一样工作
     * MAXDWORD, 0, 0: 立即返回任何当前已接收的数据，没有数据则立即返回空
     * 为了类似 select 的行为，我们在独立线程中处理读 + 睡眠
     */
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComm, &timeouts);

    return true;
}

#else

/* ============================================================
 * 内部辅助：设置串口属性（termios）
 * ============================================================ */
static bool s_configure_serial(int fd, const gh_serial_config_t* cfg) {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("[Transport] tcgetattr failed");
        return false;
    }

    /* === 波特率 ===
     * 标准波特率使用 cfsetspeed；非标准（如400000）需要平台特定处理。
     * Linux 支持通过 BOTHER + struct termios2 设置自定义波特率。
     * 此处仅展示标准路径，实际需用 ioctl(TCSETS2) 设置自定义波特率。
     */
    speed_t baud;
    bool need_custom_baud = false;
    switch (cfg->baud_rate) {
        case 9600:   baud = B9600;   break;
        case 19200:  baud = B19200;  break;
        case 38400:  baud = B38400;  break;
        case 57600:  baud = B57600;  break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
#ifdef B400000
        case 400000: baud = B400000; break;
#else
        case 400000:
            /* macOS 等平台通常无 B400000，后续通过 ioctl(IOSSIOSPEED) 设置。 */
            baud = B38400;
            need_custom_baud = true;
            break;
#endif
#ifdef B460800
        case 460800: baud = B460800; break;
#elif defined(__APPLE__)
        case 460800:
            baud = B38400;
            need_custom_baud = true;
            break;
#endif
        default:
            /* 不支持的标准波特率，尝试 B0 作为 fallback */
            baud = B115200;
            printf("[Transport] Warning: non-standard baud %d, using 115200\n",
                   cfg->baud_rate);
            break;
    }
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* 数据位 */
    tty.c_cflag &= ~CSIZE;
    switch (cfg->data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }

    /* 校验位 */
    tty.c_cflag &= ~(PARENB | PARODD);
    if (cfg->parity == 'E') {
        tty.c_cflag |= PARENB;             /* 偶校验 */
    } else if (cfg->parity == 'O') {
        tty.c_cflag |= PARENB | PARODD;    /* 奇校验 */
    }

    /* 停止位 */
    if (cfg->stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    /* 流控 */
    if (cfg->flow_control) {
        tty.c_cflag |= CRTSCTS;
    } else {
        tty.c_cflag &= ~CRTSCTS;
    }

    tty.c_cflag |= CREAD | CLOCAL; /* 使能接收，忽略调制解调器控制 */
    tty.c_lflag = 0;  /* Raw 模式：关闭规范模式、echo 等 */
    tty.c_iflag = 0;  /* 关闭软件流控、奇偶校验处理 */
    tty.c_oflag = 0;  /* 关闭输出处理 */

    /* 非阻塞读：最少读0字节，超时0（配合 select 使用）*/
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[Transport] tcsetattr failed");
        return false;
    }

#ifdef __APPLE__
    if (need_custom_baud) {
        speed_t custom_baud = (speed_t)cfg->baud_rate;
        if (ioctl(fd, IOSSIOSPEED, &custom_baud) == -1) {
            perror("[Transport] IOSSIOSPEED failed");
            return false;
        }
    }
#else
    (void)need_custom_baud;
#endif
    return true;
}
#endif

/* ============================================================
 * BLE AT 辅助
 * ============================================================ */
static void s_trim_eol(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

static bool s_is_hex_str(const char *s) {
    if (!s || *s == '\0') return false;
    size_t n = 0;
    while (s[n]) {
        if (!isxdigit((unsigned char)s[n])) return false;
        n++;
    }
    return (n > 0 && (n % 2U) == 0U);
}

static int s_hex_to_bytes(const char *hex, uint8_t *out, int out_max) {
    if (!hex || !out || out_max <= 0) return 0;
    int len = (int)strlen(hex);
    if ((len % 2) != 0) return 0;
    int n = len / 2;
    if (n > out_max) n = out_max;
    for (int i = 0; i < n; i++) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        int v1 = isdigit((unsigned char)hi) ? hi - '0' : (tolower((unsigned char)hi) - 'a' + 10);
        int v2 = isdigit((unsigned char)lo) ? lo - '0' : (tolower((unsigned char)lo) - 'a' + 10);
        out[i] = (uint8_t)((v1 << 4) | v2);
    }
    return n;
}

static int s_ble_extract_payload_bytes(const char *line, uint8_t *out, int out_max) {
    /* 兼容:
     * +BLERECV=<hex>
     * +BLERECV=<len>,<hex>
     * +BLERECV=<raw_text>
     */
    const char *p = strstr(line, "+BLERECV=");
    if (!p) return 0;
    p += 9;
    while (*p == ' ' || *p == '\t') p++;
    const char *comma = strchr(p, ',');
    const char *data = comma ? (comma + 1) : p;
    while (*data == ' ' || *data == '\t') data++;
    if (*data == '\0') return 0;

    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", data);
    s_trim_eol(tmp);

    if (s_is_hex_str(tmp)) {
        return s_hex_to_bytes(tmp, out, out_max);
    }
    /* 非 hex 时按原始字节文本传递 */
    int n = (int)strlen(tmp);
    if (n > out_max) n = out_max;
    memcpy(out, tmp, (size_t)n);
    return n;
}

static void s_ble_log_text_sanitized(gh_transport_t *t, const char *line) {
    if (!t || !t->log_cb || !line) return;
    char safe[1024];
    int off = 0;
    for (size_t i = 0; line[i] != '\0' && off < (int)sizeof(safe) - 1; i++) {
        unsigned char ch = (unsigned char)line[i];
        if (ch >= 0x20 && ch <= 0x7E) {
            safe[off++] = (char)ch;
        } else {
            if (off + 4 >= (int)sizeof(safe) - 1) break;
            safe[off++] = '\\';
            safe[off++] = 'x';
            const char hex[] = "0123456789ABCDEF";
            safe[off++] = hex[(ch >> 4) & 0x0F];
            safe[off++] = hex[ch & 0x0F];
        }
    }
    safe[off] = '\0';
    char logbuf[1200];
    snprintf(logbuf, sizeof(logbuf), "[BLE-AT] <<< %s", safe);
    t->log_cb(logbuf, t->log_ctx);
}

static void s_ble_log_recv_hex(gh_transport_t *t, const char *line, const uint8_t *payload, int n) {
    if (!t || !t->log_cb || !line) return;
    if (n <= 0 || !payload) {
        s_ble_log_text_sanitized(t, line);
        return;
    }
    const int show = (n > 128) ? 128 : n;
    char hexbuf[128 * 3 + 64];
    int off = 0;
    off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "[BLE-AT] <<< +BLERECV=");
    for (int i = 0; i < show && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "%02X", payload[i]);
        if (i + 1 < show) off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ");
    }
    if (n > show) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ...(len=%d)", n);
    }
    t->log_cb(hexbuf, t->log_ctx);
}

static void s_ble_log_raw_hex(gh_transport_t *t, const uint8_t *data, uint16_t len) {
    if (!t || !t->log_cb || !data || len == 0) return;
    const int show = (len > 96) ? 96 : (int)len;
    char hexbuf[96 * 3 + 64];
    int off = 0;
    off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "[BLE-AT] <<< RAW=");
    for (int i = 0; i < show && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "%02X", data[i]);
        if (i + 1 < show) off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ");
    }
    if ((int)len > show) off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ...(len=%u)", (unsigned)len);
    t->log_cb(hexbuf, t->log_ctx);
}

static bool s_serial_read_line(gh_transport_t *t, char *line, size_t line_sz, int timeout_ms) {
    if (!t || !line || line_sz < 2) return false;
    line[0] = '\0';
    int pos = 0;
    uint32_t start = gh_platform_now_ms();
    while ((int)(gh_platform_now_ms() - start) < timeout_ms) {
#ifdef _WIN32
        DWORD n = 0;
        char ch = 0;
        if (!ReadFile((HANDLE)t->serial_fd, &ch, 1, &n, NULL)) return false;
        if (n == 0) { GH_SLEEP_MS(5); continue; }
#else
        char ch = 0;
        ssize_t n = read((int)t->serial_fd, &ch, 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                GH_SLEEP_MS(5);
                continue;
            }
            return false;
        }
        if (n == 0) { GH_SLEEP_MS(5); continue; }
#endif
        if (pos < (int)line_sz - 1) line[pos++] = ch;
        if (ch == '\n') {
            line[pos] = '\0';
            s_trim_eol(line);
            return true;
        }
    }
    line[pos] = '\0';
    s_trim_eol(line);
    return pos > 0;
}

static bool s_ble_send_at_cmd(gh_transport_t *t, const char *cmd, const char *expect1, const char *expect2, int timeout_ms) {
    if (!t || !cmd) return false;
    char tx[256];
    snprintf(tx, sizeof(tx), "%s\r\n", cmd);
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile((HANDLE)t->serial_fd, tx, (DWORD)strlen(tx), &written, NULL)) return false;
#else
    if (write((int)t->serial_fd, tx, strlen(tx)) < 0) return false;
#endif
    if (t->log_cb) {
        char logbuf[320];
        snprintf(logbuf, sizeof(logbuf), "[BLE-AT] >>> %s", cmd);
        t->log_cb(logbuf, t->log_ctx);
    }
    uint32_t start = gh_platform_now_ms();
    while ((int)(gh_platform_now_ms() - start) < timeout_ms) {
        char line[512];
        if (!s_serial_read_line(t, line, sizeof(line), 300)) continue;
        if (line[0] == '\0') continue;
        if (t->log_cb) {
            char logbuf[600];
            snprintf(logbuf, sizeof(logbuf), "[BLE-AT] <<< %s", line);
            t->log_cb(logbuf, t->log_ctx);
        }
        if (expect1 && strstr(line, expect1)) return true;
        if (expect2 && strstr(line, expect2)) return true;
        if (strstr(line, "ERROR")) return false;
    }
    if (t->log_cb) {
        char logbuf[320];
        snprintf(logbuf, sizeof(logbuf), "[BLE-AT] timeout waiting response: %s", cmd);
        t->log_cb(logbuf, t->log_ctx);
    }
    return false;
}

static bool s_ble_send_at_cmd_retry(gh_transport_t *t, const char *cmd,
                                    const char *expect1, const char *expect2,
                                    int timeout_ms, int attempts);
static bool s_ble_wait_line_contains(gh_transport_t *t, const char *expect, int timeout_ms);

static void s_serial_drain_input(gh_transport_t *t, int duration_ms) {
    if (!t || t->serial_fd == -1 || t->serial_fd == 0) return;
    uint32_t start = gh_platform_now_ms();
    uint8_t buf[256];
    while ((int)(gh_platform_now_ms() - start) < duration_ms) {
#ifdef _WIN32
        DWORD n = 0;
        if (!ReadFile((HANDLE)t->serial_fd, buf, (DWORD)sizeof(buf), &n, NULL) || n == 0) {
            GH_SLEEP_MS(10);
        }
#else
        int n = (int)read((int)t->serial_fd, buf, sizeof(buf));
        if (n <= 0) GH_SLEEP_MS(10);
#endif
    }
}

static bool s_ble_enter_cmd_mode(gh_transport_t *t) {
    if (!t) return false;

    /* 先清空可能残留的透传噪声 */
    s_serial_drain_input(t, 120);

    /* 1) 直接探测是否已在 AT 命令态 */
    if (s_ble_send_at_cmd_retry(t, "AT", "OK", "NS-AT", 1000, 2)) return true;

    /* 2) 尝试透传逃逸序列 "+++"（不带 CRLF）*/
    if (t->log_cb) t->log_cb("[BLE-AT] try escape transparent mode: +++", t->log_ctx);
#ifdef _WIN32
    {
        DWORD written = 0;
        const char *esc = "+++";
        WriteFile((HANDLE)t->serial_fd, esc, (DWORD)strlen(esc), &written, NULL);
    }
#else
    {
        const char *esc = "+++";
        (void)write((int)t->serial_fd, esc, strlen(esc));
    }
#endif
    GH_SLEEP_MS(200);
    (void)s_ble_wait_line_contains(t, "OK", 1200);

    /* 3) 再次探测 AT 命令态 */
    if (s_ble_send_at_cmd_retry(t, "AT", "OK", "NS-AT", 1200, 3)) return true;

    if (t->log_cb) t->log_cb("[BLE-AT] failed to enter command mode", t->log_ctx);
    return false;
}

static bool s_ble_send_at_cmd_retry(gh_transport_t *t, const char *cmd,
                                    const char *expect1, const char *expect2,
                                    int timeout_ms, int attempts) {
    if (!t || !cmd || attempts <= 0) return false;
    for (int i = 0; i < attempts; i++) {
        if (s_ble_send_at_cmd(t, cmd, expect1, expect2, timeout_ms)) return true;
        if (t->log_cb) {
            char logbuf[320];
            snprintf(logbuf, sizeof(logbuf), "[BLE-AT] retry %d/%d: %s", i + 1, attempts, cmd);
            t->log_cb(logbuf, t->log_ctx);
        }
        GH_SLEEP_MS(150);
    }
    return false;
}

static bool s_ble_wait_line_contains(gh_transport_t *t, const char *expect, int timeout_ms) {
    if (!t || !expect || expect[0] == '\0') return false;
    uint32_t start = gh_platform_now_ms();
    while ((int)(gh_platform_now_ms() - start) < timeout_ms) {
        char line[512];
        if (!s_serial_read_line(t, line, sizeof(line), 300)) continue;
        if (line[0] == '\0') continue;
        if (t->log_cb) {
            char logbuf[600];
            snprintf(logbuf, sizeof(logbuf), "[BLE-AT] <<< %s", line);
            t->log_cb(logbuf, t->log_ctx);
        }
        if (strstr(line, expect)) return true;
    }
    return false;
}

static bool s_ble_scan_mac(gh_transport_t *t, const char *slave_name, char *out_mac, size_t out_mac_sz, int timeout_ms) {
    if (!t || !slave_name || !out_mac || out_mac_sz < 2) return false;
    out_mac[0] = '\0';
    if (!s_ble_send_at_cmd(t, "AT+SCAN=1", "+REPORT=", NULL, 2000)) {
        /* SCAN 命令下发可能先仅回 OK，继续读扫描结果 */
    }

    char prefix[128];
    snprintf(prefix, sizeof(prefix), "+REPORT=%s,", slave_name);
    uint32_t start = gh_platform_now_ms();
    while ((int)(gh_platform_now_ms() - start) < timeout_ms) {
        char line[512];
        if (!s_serial_read_line(t, line, sizeof(line), 500)) continue;
        if (line[0] == '\0') continue;
        if (t->log_cb) {
            char logbuf[600];
            snprintf(logbuf, sizeof(logbuf), "[BLE-AT] <<< %s", line);
            t->log_cb(logbuf, t->log_ctx);
        }
        const char *p = strstr(line, prefix);
        if (p) {
            p += strlen(prefix);
            size_t n = 0;
            while (p[n] && p[n] != ',' && !isspace((unsigned char)p[n]) && n < out_mac_sz - 1) n++;
            memcpy(out_mac, p, n);
            out_mac[n] = '\0';
            return n > 0;
        }
    }
    return false;
}

static bool s_ble_at_prepare(gh_transport_t *t) {
    if (!t) return false;
    if (!s_ble_enter_cmd_mode(t)) return false;
    if (t->log_cb) t->log_cb("[BLE-AT] skip AT+RESTORE by config", t->log_ctx);
    if (!s_ble_send_at_cmd_retry(t, "AT+ROLE=1", "OK", NULL, 3000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+SVCUID=0E19", "OK", NULL, 3000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+TXUID=0300", "OK", NULL, 3000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+RXUID=0400", "OK", NULL, 3000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+MTU=247", "OK", NULL, 3000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+CONNINTV=30", "OK", NULL, 2000, 2)) return false;
    if (!s_ble_send_at_cmd_retry(t, "AT+RESET", "OK", "NS-AT", 3500, 2)) return false;
    if (!s_ble_wait_line_contains(t, "NS-AT", 5000)) {
        if (t->log_cb) t->log_cb("[BLE-AT] WARN: wait NS-AT after RESET timeout", t->log_ctx);
    }
    return true;
}

/* ============================================================
 * 内部：帧分割逻辑（基于长度字节的精确分帧）
 *
 * 帧格式: [AA][11][len][payload: len bytes][CRC]
 * 总长度 = len + 4
 *
 * 算法：
 *   1. 在缓冲区头部定位 0xAA11
 *   2. 读取 byte[2] 作为 payload 长度，计算总帧长 = len+4
 *   3. 若已收到完整帧字节，立即回调并前进 rx_head
 *   4. 否则等待更多数据（不使用超时推送残帧）
 *
 * 相比"找下一个AA11"的方式，此方法能正确处理负载中含有 AA 11 字节的情况
 * ============================================================ */
static bool s_rpc_crc_ok(const uint8_t *frm, uint16_t total_len) {
    if (!frm || total_len < 4U) return false;
    uint16_t payload_len = (uint16_t)frm[2];
    if ((uint16_t)(payload_len + 4U) != total_len) return false;
    uint8_t crc = 0;
    for (uint16_t i = 3U; i < (uint16_t)(3U + payload_len); i++) {
        crc = (uint8_t)(crc + frm[i]);
    }
    return crc == frm[3U + payload_len];
}

static void s_log_crc_mismatch_detail(gh_transport_t *t, const uint8_t *buf,
                                      uint16_t data_len, uint16_t head,
                                      uint32_t cnt) {
    if (!t || !t->log_cb || !buf || data_len < 4U) return;
    uint16_t payload_len = (uint16_t)buf[2];
    uint16_t total_len = (uint16_t)(payload_len + 4U);
    if (data_len < total_len) return;

    uint8_t calc_crc = 0;
    for (uint16_t i = 3U; i < (uint16_t)(3U + payload_len); i++) {
        calc_crc = (uint8_t)(calc_crc + buf[i]);
    }
    uint8_t rx_crc = buf[3U + payload_len];

    char meta[192];
    snprintf(meta, sizeof(meta),
             "[Transport][CRC_ERR] cnt=%u head=%u payload_len=%u total_len=%u calc=0x%02X rx=0x%02X",
             (unsigned)cnt, (unsigned)head, (unsigned)payload_len, (unsigned)total_len,
             (unsigned)calc_crc, (unsigned)rx_crc);
    t->log_cb(meta, t->log_ctx);

    const uint16_t show = (data_len > 64U) ? 64U : data_len;
    char hexbuf[64 * 3 + 64];
    int off = 0;
    off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "[Transport][CRC_ERR] bytes=");
    for (uint16_t i = 0; i < show && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, "%02X", buf[i]);
        if (i + 1U < show) off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ");
    }
    if (data_len > show) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - (size_t)off, " ...(avail=%u)", (unsigned)data_len);
    }
    t->log_cb(hexbuf, t->log_ctx);
}

static void s_process_rx_buffer(gh_transport_t* t) {
    static uint32_t s_crc_mismatch_cnt = 0;
    while (true) {
        uint16_t data_len = (t->rx_tail >= t->rx_head)
                          ? (uint16_t)(t->rx_tail - t->rx_head)
                          : 0U;

        if (data_len < 4U) break; /* 至少需要 AA 11 len CRC = 4 字节 */

        uint8_t *buf = &t->rx_buf[t->rx_head];

        /* 若不是 AA 11，逐字节跳过直到找到帧头 */
        if (buf[0] != GH_FRAME_MARKER0 || buf[1] != GH_FRAME_MARKER1) {
            uint16_t skip = data_len; /* 默认跳过全部 */
            for (uint16_t i = 1U; i + 1U < data_len; i++) {
                if (buf[i] == GH_FRAME_MARKER0 && buf[i + 1U] == GH_FRAME_MARKER1) {
                    skip = i;
                    break;
                }
            }
            t->rx_head += skip;
            if (skip == data_len) break; /* 未找到 AA 11 */
            continue;
        }

        /* 读 payload 长度，计算完整帧字节数 */
        uint16_t payload_len  = (uint16_t)buf[2];
        uint16_t total_len    = payload_len + 4U; /* AA+11+len+payload+CRC */

        /* 防御：单帧过大（大于缓冲区一半），认为同步丢失，跳过 AA */
        if (total_len > (GH_TRANSPORT_RX_BUF_SIZE / 2U)) {
            t->rx_head += 2U;
            continue;
        }

        if (data_len < total_len) {
            /* 帧尚未完整到达，等待 */
            break;
        }

        /* 新增：先在 transport 层校验 Cardiff RPC 校验和。
         * 若校验失败，说明大概率发生了错位（BLE 分包/插入噪声），
         * 采用 1 字节滑窗重同步，而不是把坏帧整包上抛。 */
        if (!s_rpc_crc_ok(buf, total_len)) {
            s_crc_mismatch_cnt++;
            if (t->log_cb && (s_crc_mismatch_cnt <= 10U || (s_crc_mismatch_cnt % 100U) == 0U)) {
                char log_buf[96];
                snprintf(log_buf, sizeof(log_buf),
                         "[Transport] frame crc mismatch(cnt=%u) head=%u len=%u, fast-resync",
                         (unsigned)s_crc_mismatch_cnt, (unsigned)t->rx_head, (unsigned)total_len);
                t->log_cb(log_buf, t->log_ctx);
                s_log_crc_mismatch_detail(t, buf, data_len, t->rx_head, s_crc_mismatch_cnt);
            }
            /* 关键修复：
             * payload 内部也可能出现 AA11，不能盲目跳到下一个 AA11。
             * 仅当候选 AA11 对应的完整帧在当前缓冲内且 CRC 可通过时才跳转；
             * 否则采用 +1 滑窗，避免误锁导致大段丢帧。 */
            uint16_t skip = 1U;
            bool found_valid = false;
            for (uint16_t i = 1U; i + 3U < data_len; i++) {
                if (buf[i] != GH_FRAME_MARKER0 || buf[i + 1U] != GH_FRAME_MARKER1) continue;
                uint16_t cand_payload = (uint16_t)buf[i + 2U];
                uint16_t cand_total = (uint16_t)(cand_payload + 4U);
                if (cand_total > (GH_TRANSPORT_RX_BUF_SIZE / 2U)) continue;
                if ((uint16_t)(data_len - i) < cand_total) continue; /* 候选帧还不完整，继续找 */
                if (s_rpc_crc_ok(&buf[i], cand_total)) {
                    skip = i;
                    found_valid = true;
                    break;
                }
            }
            if (!found_valid && t->log_cb && (s_crc_mismatch_cnt <= 10U || (s_crc_mismatch_cnt % 200U) == 0U)) {
                t->log_cb("[Transport][CRC_ERR] no valid AA11 candidate in current buffer, slide+1", t->log_ctx);
            }
            t->rx_head += skip;
            continue;
        }

        /* 完整帧：回调上层 */
        if (t->on_frame) {
            t->on_frame(buf, total_len, t->on_frame_ctx);
        }
        t->rx_head += total_len;

        /* 缓冲区紧凑：head 超过半程时搬移数据 */
        if (t->rx_head >= (GH_TRANSPORT_RX_BUF_SIZE / 2U)) {
            uint16_t remaining = t->rx_tail - t->rx_head;
            if (remaining > 0U) {
                memmove(t->rx_buf, &t->rx_buf[t->rx_head], remaining);
            }
            t->rx_head = 0U;
            t->rx_tail = remaining;
        }
    }

    t->last_rx_time_ms    = gh_platform_now_ms();
    t->frame_timeout_armed = (t->rx_tail > t->rx_head);
}

/**
 * @brief 帧超时：丢弃不完整的残余数据（避免积压）
 */
static void s_check_frame_timeout(gh_transport_t* t) {
    if (!t->frame_timeout_armed) return;

    uint32_t now = gh_platform_now_ms();
    /* BLE 透传分包间隔显著大于物理串口，超时窗口需放宽，避免半帧被误丢弃导致 CRC 错 */
    uint32_t timeout_ms = t->ble_at_mode ? 5000U : GH_FRAME_TIMEOUT_MS;
    if (now - t->last_rx_time_ms < timeout_ms) return;

    t->frame_timeout_armed = false;
    /* 长度模式下无法可靠拼接残帧，直接丢弃 */
    t->rx_head = t->rx_tail = 0U;
}

static void s_feed_frame_parser(gh_transport_t *t, const uint8_t *data, uint16_t len) {
    if (!t || !data || len == 0) return;
    uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
    if (len > space) {
        if (t->rx_head > 0) {
            memmove(t->rx_buf, &t->rx_buf[t->rx_head], t->rx_tail - t->rx_head);
            t->rx_tail -= t->rx_head;
            t->rx_head = 0;
            space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
        }
    }
    if (len > space) {
        if (t->log_cb) t->log_cb("[Transport] frame parser overflow, drop packet", t->log_ctx);
        return;
    }
    memcpy(&t->rx_buf[t->rx_tail], data, len);
    t->rx_tail += len;
    s_process_rx_buffer(t);
}

static bool s_is_ascii_text_byte(uint8_t b) {
    return (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20U && b <= 0x7EU));
}

static void s_flush_ble_text_line(gh_transport_t *t) {
    if (!t || t->ble_line_len == 0U) return;
    t->ble_line_buf[t->ble_line_len] = '\0';
    s_trim_eol(t->ble_line_buf);
    if (t->ble_line_buf[0] != '\0') {
        if (strstr(t->ble_line_buf, "+BLERECV=") == t->ble_line_buf) {
            uint8_t payload[4096];
            int n = s_ble_extract_payload_bytes(t->ble_line_buf, payload, (int)sizeof(payload));
            s_ble_log_recv_hex(t, t->ble_line_buf, payload, n);
            if (n > 0) s_feed_frame_parser(t, payload, (uint16_t)n);
        } else {
            s_ble_log_text_sanitized(t, t->ble_line_buf);
        }
    }
    t->ble_line_len = 0U;
    t->ble_line_buf[0] = '\0';
}

static void s_process_ble_stream(gh_transport_t *t, const uint8_t *data, uint16_t len) {
    if (!t || !data || len == 0) return;

    /* 透传阶段优先按原始二进制处理，避免把 payload 误判成文本行。 */
    bool has_blerecv = false;
    if (len >= 9) {
        for (uint16_t i = 0; i + 9 <= len; i++) {
            if (memcmp(&data[i], "+BLERECV=", 9) == 0) { has_blerecv = true; break; }
        }
    }

    if (!has_blerecv && t->ble_line_len == 0U) {
        s_feed_frame_parser(t, data, len);
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        char ch = (char)b;

        /* 空行状态遇到明显二进制，直接切 raw，避免被文本机吞掉。 */
        if (t->ble_line_len == 0U && !has_blerecv && !s_is_ascii_text_byte(b)) {
            s_feed_frame_parser(t, &data[i], (uint16_t)(len - i));
            return;
        }

        /* 已累积文本但中途出现二进制：认为 AT 行已结束，切 raw。 */
        if (t->ble_line_len > 0U && !s_is_ascii_text_byte(b)) {
            s_flush_ble_text_line(t);
            s_feed_frame_parser(t, &data[i], (uint16_t)(len - i));
            return;
        }

        if (t->ble_line_len < (GH_BLE_LINE_BUF_SIZE - 1U)) {
            t->ble_line_buf[t->ble_line_len++] = ch;
        } else {
            /* 文本行超长通常是误判，尽快回退到 raw。 */
            if (t->log_cb) t->log_cb("[BLE-AT] text line overflow, switch raw parser", t->log_ctx);
            s_flush_ble_text_line(t);
            if (!has_blerecv) {
                s_feed_frame_parser(t, &data[i], (uint16_t)(len - i));
                return;
            }
        }

        if (ch == '\n') {
            s_flush_ble_text_line(t);
            if ((i + 1U) < len && !has_blerecv && !s_is_ascii_text_byte(data[i + 1U])) {
                s_feed_frame_parser(t, &data[i + 1U], (uint16_t)(len - i - 1U));
                return;
            }
        }
    }
}

/* ============================================================
 * 接收线程（替代 Qt 事件循环 + readyRead 信号）
 * ============================================================ */
#ifdef _WIN32
static unsigned __stdcall s_rx_thread(void* arg) {
    gh_transport_t* t = (gh_transport_t*)arg;

    while (t->rx_thread_running) {
        if (t->ble_at_mode) {
            uint8_t read_buf[4096];
            DWORD bytesRead = 0;
            if (ReadFile((HANDLE)t->serial_fd, read_buf, (DWORD)sizeof(read_buf), &bytesRead, NULL)) {
                if (bytesRead > 0) {
                    s_process_ble_stream(t, read_buf, (uint16_t)bytesRead);
                }
            }
            s_check_frame_timeout(t);
            GH_SLEEP_MS(1);
            continue;
        }

        DWORD bytesRead = 0;
        uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
        if (space == 0) {
            if (t->rx_head > 0) {
                memmove(t->rx_buf, &t->rx_buf[t->rx_head], t->rx_tail - t->rx_head);
                t->rx_tail -= t->rx_head;
                t->rx_head = 0;
            } else {
                /* 缓冲区完全占满且无法再压缩，丢弃积压避免读线程卡死 */
                if (t->log_cb) t->log_cb("[Transport] RX buffer overflow, drop buffered bytes", t->log_ctx);
                t->rx_head = 0;
                t->rx_tail = 0;
                t->frame_timeout_armed = false;
            }
            space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
        }

        if (ReadFile((HANDLE)t->serial_fd, &t->rx_buf[t->rx_tail], space, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                /* 记录接收的原始字节 */
                if (t->log_cb) {
                    char hex_buf[64 * 3 + 8];
                    int off = 0;
                    off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[RX]");
                    DWORD show = bytesRead > 64 ? 64 : bytesRead;
                    for (DWORD i = 0; i < show; i++) {
                        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                        " %02X", t->rx_buf[t->rx_tail + i]);
                    }
                    if (bytesRead > 64) {
                        snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                 " ...(%lu bytes)", bytesRead);
                    }
                    t->log_cb(hex_buf, t->log_ctx);
                }
                t->rx_tail += (uint16_t)bytesRead;
                s_process_rx_buffer(t);
            }
        }
        
        s_check_frame_timeout(t);
        GH_SLEEP_MS(50);
    }
    return 0;
}
#else
static void* s_rx_thread(void* arg) {
    gh_transport_t* t = (gh_transport_t*)arg;

    while (t->rx_thread_running) {
        if (t->ble_at_mode) {
            fd_set rfds_ble;
            struct timeval tv_ble;
            int ret_ble;
            uint8_t read_buf[4096];

            FD_ZERO(&rfds_ble);
            FD_SET((int)t->serial_fd, &rfds_ble);
            tv_ble.tv_sec = 0;
            tv_ble.tv_usec = 50 * 1000;
            ret_ble = select((int)t->serial_fd + 1, &rfds_ble, NULL, NULL, &tv_ble);
            if (ret_ble < 0) {
                if (errno != EINTR && t->log_cb) t->log_cb("[Transport] select error", t->log_ctx);
                if (errno != EINTR) break;
            } else if (ret_ble > 0 && FD_ISSET((int)t->serial_fd, &rfds_ble)) {
                ssize_t n = read((int)t->serial_fd, read_buf, sizeof(read_buf));
                if (n > 0) {
                    s_process_ble_stream(t, read_buf, (uint16_t)n);
                }
            }
            s_check_frame_timeout(t);
            continue;
        }

        fd_set  rfds;
        struct timeval tv;
        int     ret;

        FD_ZERO(&rfds);
        FD_SET((int)t->serial_fd, &rfds);

        /* 使用 50ms 轮询，比 100ms 帧超时频率高，保证及时检测 */
        tv.tv_sec  = 0;
        tv.tv_usec = 50 * 1000; /* 50ms */

        ret = select((int)t->serial_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;  /* 被信号中断，继续 */
            if (t->log_cb) {
                t->log_cb("[Transport] select error", t->log_ctx);
            }
            break;
        }

        if (ret > 0 && FD_ISSET((int)t->serial_fd, &rfds)) {
            /* 有数据可读 */
            uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
            if (space == 0) {
                /* 缓冲区已满，合并或清空 */
                if (t->rx_head > 0) {
                    memmove(t->rx_buf, &t->rx_buf[t->rx_head],
                            t->rx_tail - t->rx_head);
                    t->rx_tail -= t->rx_head;
                    t->rx_head = 0;
                } else {
                    /* 满缓冲且无可压缩空间：丢弃积压，避免出现 read(...,0) 导致线程退出 */
                    if (t->log_cb) t->log_cb("[Transport] RX buffer overflow, drop buffered bytes", t->log_ctx);
                    t->rx_head = 0;
                    t->rx_tail = 0;
                    t->frame_timeout_armed = false;
                }
                space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
            }

            ssize_t n = read((int)t->serial_fd,
                             &t->rx_buf[t->rx_tail],
                             space);
            if (n > 0) {
                /* 记录接收的原始字节 */
                if (t->log_cb) {
                    char hex_buf[64 * 3 + 8];
                    int off = 0;
                    off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[RX]");
                    ssize_t show = n > 64 ? 64 : n;
                    for (ssize_t i = 0; i < show; i++) {
                        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                        " %02X", t->rx_buf[t->rx_tail + i]);
                    }
                    if (n > 64) {
                        snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                 " ...(%zd bytes)", n);
                    }
                    t->log_cb(hex_buf, t->log_ctx);
                }
                t->rx_tail += (uint16_t)n;
                s_process_rx_buffer(t);
            } else if (n == 0) {
                /* 非阻塞串口上 n==0 可能是瞬态，避免误退出接收线程 */
                continue;
            }
        }

        /* 在每次 select 返回时检查帧超时 */
        s_check_frame_timeout(t);
    }

    return NULL;
}
#endif

/* ============================================================
 * 公开API实现
 * ============================================================ */

void gh_transport_init(gh_transport_t* t,
                       gh_on_frame_cb on_frame, void* frame_ctx,
                       gh_on_connect_cb on_connect, void* conn_ctx,
                       gh_log_cb log_cb, void* log_ctx) {
    if (!t) return;
    memset(t, 0, sizeof(gh_transport_t));
    t->serial_fd      = -1;
    t->on_frame       = on_frame;
    t->on_frame_ctx   = frame_ctx;
    t->on_connect     = on_connect;
    t->on_connect_ctx = conn_ctx;
    t->log_cb         = log_cb;
    t->log_ctx        = log_ctx;
    t->rx_thread_started = false;
    t->rx_thread_running = false;
    t->ble_at_mode = false;
    t->ble_slave_name[0] = '\0';
    t->ble_mac[0] = '\0';
    t->ble_line_len = 0U;
    t->ble_line_buf[0] = '\0';
}

bool gh_transport_open_serial(gh_transport_t* t, const gh_serial_config_t* cfg) {
    if (!t || !cfg) return false;

    /* 防御：重新打开串口前确保接收线程完全退出，避免旧线程读新句柄 */
    gh_transport_stop_rx_thread(t);
    
#ifdef _WIN32
    if (t->serial_fd != 0 && t->serial_fd != -1) {
        CloseHandle((HANDLE)t->serial_fd);
        t->serial_fd = -1;
    }

    char portName[128];
    snprintf(portName, sizeof(portName), "\\\\.\\%s", cfg->port); // 解决COM10及以上的问题

    HANDLE hComm = CreateFileA(portName,
                               GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);

    if (hComm == INVALID_HANDLE_VALUE) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Transport] Cannot open %s: Error %lu",
                 cfg->port, GetLastError());
        if (t->log_cb) t->log_cb(msg, t->log_ctx);
        return false;
    }

    if (!s_configure_serial(hComm, cfg)) {
        CloseHandle(hComm);
        return false;
    }

    t->serial_fd = (intptr_t)hComm;

#else
    if (t->serial_fd >= 0) {
        close((int)t->serial_fd);
        t->serial_fd = -1;
    }

    /* 打开串口设备，O_NOCTTY 不受终端控制，O_NONBLOCK 非阻塞 */
    int fd = open(cfg->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Transport] Cannot open %s: %s",
                 cfg->port, strerror(errno));
        if (t->log_cb) t->log_cb(msg, t->log_ctx);
        return false;
    }

    if (!s_configure_serial(fd, cfg)) {
        close(fd);
        return false;
    }

    t->serial_fd = fd;
#endif

    memcpy(&t->serial_cfg, cfg, sizeof(gh_serial_config_t));
    t->rx_head = 0;
    t->rx_tail = 0;
    t->frame_timeout_armed = false;
    t->ble_at_mode = false;
    t->ble_mac[0] = '\0';
    t->ble_line_len = 0U;
    t->ble_line_buf[0] = '\0';

    if (t->on_connect) {
        t->on_connect(true, t->on_connect_ctx);
    }

    char msg_log[128];
    snprintf(msg_log, sizeof(msg_log), "[Transport] Opened %s @ %d baud",
             cfg->port, cfg->baud_rate);
    if (t->log_cb) t->log_cb(msg_log, t->log_ctx);
    return true;
}

bool gh_transport_open_ble_at(gh_transport_t* t, const gh_serial_config_t* cfg,
                              const char* slave_name, char* out_mac, size_t out_mac_sz) {
    return gh_transport_open_ble_at_with_mac(t, cfg, slave_name, NULL, out_mac, out_mac_sz);
}

bool gh_transport_open_ble_at_with_mac(gh_transport_t* t, const gh_serial_config_t* cfg,
                                       const char* slave_name, const char* mac_in,
                                       char* out_mac, size_t out_mac_sz) {
    if (!t || !cfg || !slave_name || slave_name[0] == '\0') return false;
    if (!gh_transport_open_serial(t, cfg)) return false;
    if (!s_ble_at_prepare(t)) {
        gh_transport_close(t);
        return false;
    }

    char mac[32];
    mac[0] = '\0';
    if (mac_in && mac_in[0] != '\0') {
        snprintf(mac, sizeof(mac), "%s", mac_in);
    } else {
        if (!s_ble_scan_mac(t, slave_name, mac, sizeof(mac), 10000)) {
            gh_transport_close(t);
            return false;
        }
    }

    char conn_cmd[64];
    snprintf(conn_cmd, sizeof(conn_cmd), "AT+CONN=%s", mac);
    if (!s_ble_send_at_cmd(t, conn_cmd, "+CONN=", "OK", 6000)) {
        gh_transport_close(t);
        return false;
    }
    if (!s_ble_send_at_cmd(t, "AT+EXIT=1", "OK", NULL, 1500)) {
        gh_transport_close(t);
        return false;
    }

    t->ble_at_mode = true;
    snprintf(t->ble_slave_name, sizeof(t->ble_slave_name), "%s", slave_name);
    snprintf(t->ble_mac, sizeof(t->ble_mac), "%s", mac);
    if (out_mac && out_mac_sz > 0) snprintf(out_mac, out_mac_sz, "%s", mac);
    if (t->log_cb) {
        char logbuf[128];
        snprintf(logbuf, sizeof(logbuf), "[BLE-AT] connected slave=%s mac=%s", t->ble_slave_name, t->ble_mac);
        t->log_cb(logbuf, t->log_ctx);
    }
    return true;
}

bool gh_transport_open_ble_at_fast(gh_transport_t* t, const gh_serial_config_t* cfg,
                                   const char* slave_name, const char* mac,
                                   char* out_mac, size_t out_mac_sz) {
    if (!t || !cfg || !slave_name || slave_name[0] == '\0' || !mac || mac[0] == '\0') return false;
    if (!gh_transport_open_serial(t, cfg)) return false;
    if (!s_ble_enter_cmd_mode(t)) {
        gh_transport_close(t);
        return false;
    }

    char conn_cmd[64];
    snprintf(conn_cmd, sizeof(conn_cmd), "AT+CONN=%s", mac);
    if (!s_ble_send_at_cmd_retry(t, conn_cmd, "+CONN=", "OK", 6000, 2)) {
        gh_transport_close(t);
        return false;
    }
    if (!s_ble_send_at_cmd_retry(t, "AT+EXIT=1", "OK", NULL, 2000, 2)) {
        gh_transport_close(t);
        return false;
    }

    t->ble_at_mode = true;
    snprintf(t->ble_slave_name, sizeof(t->ble_slave_name), "%s", slave_name);
    snprintf(t->ble_mac, sizeof(t->ble_mac), "%s", mac);
    if (out_mac && out_mac_sz > 0) snprintf(out_mac, out_mac_sz, "%s", mac);
    if (t->log_cb) {
        char logbuf[128];
        snprintf(logbuf, sizeof(logbuf), "[BLE-AT] fast connected slave=%s mac=%s", t->ble_slave_name, t->ble_mac);
        t->log_cb(logbuf, t->log_ctx);
    }
    return true;
}

bool gh_transport_ble_scan(gh_transport_t* t, const gh_serial_config_t* cfg,
                           const char* slave_name, char* out_mac, size_t out_mac_sz) {
    if (!t || !cfg || !slave_name || slave_name[0] == '\0' || !out_mac || out_mac_sz < 2) return false;
    out_mac[0] = '\0';
    if (!gh_transport_open_serial(t, cfg)) return false;
    if (!s_ble_at_prepare(t)) {
        gh_transport_close(t);
        return false;
    }
    if (!s_ble_scan_mac(t, slave_name, out_mac, out_mac_sz, 10000)) {
        gh_transport_close(t);
        return false;
    }
    gh_transport_close(t);
    return true;
}

void gh_transport_close(gh_transport_t* t) {
    if (!t) return;
    gh_transport_stop_rx_thread(t);
    
#ifdef _WIN32
    if (t->serial_fd != -1 && t->serial_fd != 0) {
        CloseHandle((HANDLE)t->serial_fd);
        t->serial_fd = -1;
    }
#else
    if (t->serial_fd >= 0) {
        close((int)t->serial_fd);
        t->serial_fd = -1;
    }
#endif

    if (t->on_connect) {
        t->on_connect(false, t->on_connect_ctx);
    }
    t->ble_line_len = 0U;
    t->ble_line_buf[0] = '\0';
}

bool gh_transport_send(gh_transport_t* t, const uint8_t* data, uint16_t len) {
    if (!t || t->serial_fd == -1 || t->serial_fd == 0 || !data || len == 0) return false;

    /* 记录发送的原始字节（十六进制），通过 log_cb 推送到调试窗口 */
    if (t->log_cb) {
        /* 格式: "[TX] AA 11 55 01 19 01 xx" (最多显示64字节) */
        char hex_buf[64 * 3 + 8];
        int  off = 0;
        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[TX]");
        uint16_t show = len > 64 ? 64 : len;
        for (uint16_t i = 0; i < show; i++) {
            off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                            " %02X", data[i]);
        }
        if (len > 64) {
            off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                            " ...(%u bytes)", (unsigned)len);
        }
        t->log_cb(hex_buf, t->log_ctx);
    }

#ifdef _WIN32
    DWORD written = 0;
    if (WriteFile((HANDLE)t->serial_fd, data, len, &written, NULL)) {
        return (written == len);
    }
    return false;
#else
    ssize_t written = write((int)t->serial_fd, data, len);
    return (written == (ssize_t)len);
#endif
}

bool gh_transport_is_open(const gh_transport_t* t) {
    return (t && t->serial_fd != -1 && t->serial_fd != 0);
}

void gh_transport_feed(gh_transport_t* t, const uint8_t* data, uint16_t len) {
    if (!t || !data || len == 0) return;
    /* 此函数供不使用线程时调用，手动喂数据 */
    uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
    if (len > space) len = space;
    memcpy(&t->rx_buf[t->rx_tail], data, len);
    t->rx_tail += len;
    s_process_rx_buffer(t);
}

bool gh_transport_start_rx_thread(gh_transport_t* t) {
    gh_thread_t tid;
    if (!t) return false;
    if (t->rx_thread_started && t->rx_thread_running) {
        return true;
    }
    t->rx_thread_running = true;
    
#ifdef _WIN32
    tid = (HANDLE)_beginthreadex(NULL, 0, s_rx_thread, t, 0, NULL);
    if (tid == 0) {
        t->rx_thread_running = false;
        return false;
    }
    t->rx_thread = tid;
    t->rx_thread_started = true;
#else
    if (pthread_create(&tid, NULL, s_rx_thread, t) != 0) {
        t->rx_thread_running = false;
        return false;
    }
    t->rx_thread = tid;
    t->rx_thread_started = true;
#endif

    return true;
}

void gh_transport_stop_rx_thread(gh_transport_t* t) {
    if (!t) return;
    t->rx_thread_running = false;
    if (!t->rx_thread_started) return;

#ifdef _WIN32
    /* 线程循环每50ms检查标志位，INFINITE等待可确保完全退出 */
    WaitForSingleObject((HANDLE)t->rx_thread, INFINITE);
    CloseHandle((HANDLE)t->rx_thread);
#else
    pthread_join(t->rx_thread, NULL);
#endif
    t->rx_thread_started = false;
}
