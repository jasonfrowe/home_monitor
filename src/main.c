#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>

#include <unistd.h>

/* cc65 needs explicit prototype for strstr */
char *strstr(const char *haystack, const char *needle);

#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 60UL
#endif

/* --- Configuration --- */
#define BUFFER_SIZE 8192
#define UPDATE_INTERVAL_MIN 5
#define TICKS_PER_MIN (60UL * CLOCKS_PER_SEC) 
#define TIMEZONE_OFFSET -5      /* EST = -5, EDT = -4 */

/* --- Keyboard / XRAM Configuration --- */
#define KEYBOARD_INPUT  0xEC20  // XRAM address for keyboard data
#define KEYBOARD_BYTES  32      // 32 bytes for 256 key states
#define KEY_ESC 0x29   
// Macro to check if a key is pressed
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

/* RIA Opcodes */
#ifndef RIA_OP_READ_XRAM
#define RIA_OP_READ_XRAM 0x06
#endif
#ifndef RIA_OP_READ_XSTACK
#define RIA_OP_READ_XSTACK 0x0B
#endif

/* --- ANSI Color Macros --- */
#define ANSI_CLS        "\x1b[2J\x1b[H"
#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD       "\x1b[1m"
#define ANSI_CYAN       "\x1b[36m"
#define ANSI_GREEN      "\x1b[32m"
#define ANSI_YELLOW     "\x1b[33m"
#define ANSI_WHITE      "\x1b[37m"
#define ANSI_MAGENTA    "\x1b[35m"

/* --- Globals --- */
static char g_buffer[BUFFER_SIZE];
static char g_read_temp[256];
static char g_temp_line[128];
uint8_t keystates[KEYBOARD_BYTES] = {0};

static int is_entity(const char* p, const char* entity) {
    return strncmp(p, entity, strlen(entity)) == 0;
}

static void print_pretty_line(const char* start, const char* end) {
    const char* p = start;
    int is_value = 0;
    
    /* Consume initial whitespace */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    if (p >= end) return;

    printf(ANSI_CYAN);

    while (p < end) {
        if (*p == '&') {
            if (is_entity(p, "&#176;")) {
                printf(ANSI_YELLOW " deg " ANSI_CYAN);
                if (is_value) printf(ANSI_BOLD ANSI_WHITE);
                p += 6; continue;
            }
            if (is_entity(p, "&quot;")) { putchar('"'); p += 6; continue; }
            if (is_entity(p, "&amp;"))  { putchar('&'); p += 5; continue; }
            if (is_entity(p, "&lt;"))   { putchar('<'); p += 4; continue; }
            if (is_entity(p, "&gt;"))   { putchar('>'); p += 4; continue; }
        }

        if (*p == ':' && !is_value) {
            printf(":%s ", ANSI_RESET);
            is_value = 1;
            printf(ANSI_BOLD ANSI_WHITE);
            p++;
            continue;
        }

        if (*p == ';') {
            printf(ANSI_RESET ";\n" ANSI_CYAN); 
            is_value = 0;
            p++;
            /* Consume ALL whitespace after semicolon */
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                p++;
            }
            continue;
        }

        if (*p != '\n' && *p != '\r') {
            putchar(*p);
        }
        p++;
    }
    printf(ANSI_RESET "\n");
}

static int modem_read_char(int fd, char* ch, unsigned long timeout) {
    unsigned long start = clock();
    int count;
    while ((clock() - start) < timeout) {
        ria_push_char(1);
        ria_set_ax(fd);
        count = ria_call_int(RIA_OP_READ_XSTACK);
        if (count == 1) {
            *ch = ria_pop_char();
            return 1;
        }
    }
    return 0;
}

static void modem_send(int fd, const char* str) {
    int len, i;
    len = strlen(str);
    for (i = len - 1; i >= 0; i--) ria_push_char(str[i]);
    ria_set_ax(fd);
    ria_call_int(RIA_OP_WRITE_XSTACK);
}

static int modem_read_bulk(int fd, char* buf, int max_len, unsigned long timeout) {
    unsigned long start = clock();
    int total = 0;
    int count, i, chunk;
    
    while (total < max_len && (clock() - start) < timeout) {
        chunk = max_len - total;
        if (chunk > 256) chunk = 256;
        ria_push_int(chunk);
        ria_set_ax(fd);
        count = ria_call_int(RIA_OP_READ_XSTACK);
        if (count > 0) {
            for (i = 0; i < count; i++) g_read_temp[i] = ria_pop_char();
            for (i = 0; i < count; i++) buf[total++] = g_read_temp[i];
            start = clock();
        }
    }
    return total;
}

static int fetch_data(void) {
    int fd, bytes_read, pos;
    char *tag_start, *tag_end;
    unsigned long start;
    int line_len;
    char ch;
    int block_count = 0;
    time_t now;
    struct tm *t;
    char time_str[40];

    fd = open("AT:", O_RDWR);
    if (fd < 0) {
        printf(ANSI_MAGENTA "Error: Modem not ready.\n" ANSI_RESET);
        return 0;
    }

    modem_send(fd, "ATZ\r\n");
    modem_read_bulk(fd, g_buffer, 256, 200);
    modem_send(fd, "ATE0\r\n");
    modem_read_bulk(fd, g_buffer, 256, 200);

    printf("Connecting...");
    
    modem_send(fd, "ATDweatherpi.home.arpa:80\r\n");

    start = clock();
    while ((clock() - start) < 5000) {
        line_len = 0;
        while (line_len < 63 && (clock() - start) < 5000) {
             int res = modem_read_char(fd, &ch, 50);
             if (res == 1) {
                 if (ch == '\n' || ch == '\r') break;
                 g_temp_line[line_len++] = ch;
             }
        }
        g_temp_line[line_len] = '\0';
        if (strstr(g_temp_line, "CONNECT")) break;
        if (strstr(g_temp_line, "NO CARRIER")) {
            printf("\n" ANSI_MAGENTA "Connection Failed." ANSI_RESET "\n");
            close(fd);
            return 0;
        }
    }

    printf("\rConnected. Downloading...         ");
    
    modem_send(fd, "GET /weewx/rss.xml HTTP/1.1\r\n");
    modem_send(fd, "Host: weatherpi.home.arpa\r\n");
    modem_send(fd, "Connection: close\r\n\r\n");

    bytes_read = modem_read_bulk(fd, g_buffer, BUFFER_SIZE - 1, 3000);
    g_buffer[bytes_read] = '\0';
    close(fd);

    /* --- CAPTURE & FORMAT TIME --- */
    now = time(NULL);
    /* Adjust UTC to Local Time manually */
    now += (TIMEZONE_OFFSET * 3600);
    t = localtime(&now);
    /* Format: "Nov 25 22:30" */
    strftime(time_str, sizeof(time_str), "%b %d %H:%M", t);
    /* ----------------------------- */

    printf(ANSI_CLS);
    // printf(ANSI_GREEN "WeatherPi Monitor" ANSI_RESET " (Last update: Now)\n");
    printf(ANSI_GREEN "WeatherPi Monitor" ANSI_RESET " (Last update: %s)\n", time_str);
    printf("----------------------------------------\n\n");

    pos = 0;
    while (pos < bytes_read) {
        tag_start = strstr(g_buffer + pos, "<description>");
        if (!tag_start) break;
        tag_start += 13;
        tag_end = strstr(tag_start, "</description>");
        if (!tag_end) break;
        
        *tag_end = '\0'; /* Terminate string temporarily for processing */
        
        /* --- FILTERING LOGIC --- */
        
        /* 1. Skip Channel Title (usually contains "summaries") */
        if (strstr(tag_start, "summaries") != NULL) {
            pos = (int)(tag_end - g_buffer) + 14;
            continue;
        }
        
        /* 2. Skip Monthly and Yearly summaries */
        if (strstr(tag_start, "total for month") != NULL || 
            strstr(tag_start, "total for year") != NULL) {
            pos = (int)(tag_end - g_buffer) + 14;
            continue;
        }
        
        /* --- PRINTING --- */
        
        /* Optional: Add headers based on order found */
        if (block_count == 0) printf(ANSI_YELLOW "CURRENT CONDITIONS:\n" ANSI_RESET);
        else if (block_count == 1) printf(ANSI_YELLOW "\nDAILY SUMMARY:\n" ANSI_RESET);
        
        print_pretty_line(tag_start, tag_end);
        printf("\n");
        block_count++;
        
        pos = (int)(tag_end - g_buffer) + 14;
    }

    return 1;
}

void main(void) {
    unsigned long timer_start;
    unsigned long next_update_ticks = UPDATE_INTERVAL_MIN * TICKS_PER_MIN;
    uint8_t j;

    // Enable keyboard input
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);

    printf(ANSI_CLS);
    printf("Initializing Weather Monitor...\n\n");

    j = 1;
    while(j == 1) {
        fetch_data();

        printf(ANSI_YELLOW "\nNext update in %d minutes.\n", UPDATE_INTERVAL_MIN);
        printf("Press ESC to exit.\n" ANSI_RESET);

        timer_start = clock();
        while ((clock() - timer_start) < next_update_ticks) {
            // Read all keyboard state bytes
            uint8_t i;
            RIA.addr0 = KEYBOARD_INPUT;
            RIA.step0 = 1;
            for (i = 0; i < KEYBOARD_BYTES; i++) {
                keystates[i] = RIA.rw0;
            }

            // Check for ESC key to exit
            if (key(KEY_ESC)) {
                printf("Exiting ...\n");
                j = 0;
                break;
            }
        }
    }
}