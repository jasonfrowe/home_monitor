#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

/* ANSI Color Codes */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_CLS     "\x1b[2J\x1b[H" /* Clear Screen + Home Cursor */

/* cc65 prototypes */
char *strstr(const char *haystack, const char *needle);

#ifndef RIA_OP_READ_XSTACK
#define RIA_OP_READ_XSTACK 0x0B
#endif

/* 8KB Buffer to fit Headers + XML Body */
#define BUFFER_SIZE 8192

static char g_buffer[BUFFER_SIZE];
static char g_temp[64];
static char g_read_temp[256];

/* Read single char */
static int modem_read_char(int fd, char* ch, unsigned long timeout)
{
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

/* 
 * Send string (Reversed for Stack LIFO) 
 * We push the LAST char first, so the FIRST char ends up on Top.
 */
static void modem_send(int fd, const char* str)
{
    int len, i;
    len = strlen(str);
    for (i = len - 1; i >= 0; i--) {
        ria_push_char(str[i]);
    }
    ria_set_ax(fd);
    ria_call_int(RIA_OP_WRITE_XSTACK);
}

/* 
 * FIXED: Bulk read 
 * The Kernel puts the First Byte at the Top of the stack.
 * We pop linearly (0 to count) to preserve order.
 */
static int modem_read(int fd, char* buf, int max_len, unsigned long timeout)
{
    unsigned long start;
    int total, count, i;
    int chunk;
    
    total = 0;
    start = clock();
    
    while (total < max_len && (clock() - start) < timeout) {
        chunk = max_len - total;
        if (chunk > 256) chunk = 256;
        
        ria_push_int(chunk);
        ria_set_ax(fd);
        count = ria_call_int(RIA_OP_READ_XSTACK);
        
        if (count > 0) {
            /* 
             * Stack Top = First Byte.
             * Pop directly into the temp buffer in order.
             */
            for (i = 0; i < count; i++) {
                g_read_temp[i] = ria_pop_char();
            }
            
            /* Append to main buffer */
            for (i = 0; i < count; i++) {
                buf[total++] = g_read_temp[i];
            }
            start = clock(); /* Reset timeout on activity */
        }
    }
    return total;
}

static void print_clean(const char* str)
{
    const char* p = str;
    int is_value = 0; /* 0 = Printing Label, 1 = Printing Value */
    int new_line = 1;

    /* Skip initial blank lines */
    while (*p && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) {
        p++;
    }

    while (*p) {
        /* Start of a new line: Reset color and assume Label */
        if (new_line) {
            /* Check for thematic keywords in the upcoming line */
            if (strstr(p, "Rain") == p || strstr(p, "rain") == p) 
                printf(ANSI_BLUE);
            else if (strstr(p, "Wind") == p) 
                printf(ANSI_CYAN);
            else if (strstr(p, "temp") == p || strstr(p, "Temp") == p) 
                printf(ANSI_YELLOW);
            else 
                printf(ANSI_BOLD ANSI_MAGENTA); /* Default Label Color */
                
            is_value = 0;
            new_line = 0;
        }

        /* Decode HTML Degree Symbol */
        if (strncmp(p, "&#176;", 6) == 0) {
            printf("\xF8"); /* \xF8 is the standard degree symbol in many CP437/ISO fonts, or use " deg" */
            p += 6;
            continue;
        }
        
        /* Handle Newlines */
        if (*p == '\n') {
            printf(ANSI_RESET "\n"); /* Reset color at end of line */
            new_line = 1;
            /* Skip leading indentation on next line */
            while (p[1] == ' ' || p[1] == '\t') p++;
        }
        /* Handle the separator ':' */
        else if (*p == ':' && !is_value) {
            printf(":%s", ANSI_GREEN); /* Switch to Value Color (Green) */
            is_value = 1;
        }
        /* Handle semicolon ';' which often separates values on one line */
        else if (*p == ';') {
            printf(ANSI_RESET ";\n"); /* Treat semicolon as a hard break for readability */
            new_line = 1;
             /* Skip space after semicolon if present */
            while (p[1] == ' ') p++;
        }
        else {
            putchar(*p);
        }
        p++;
    }
    printf(ANSI_RESET "\n");
}

static void fetch_weather_rss(void)
{
    int fd, bytes_read, pos;
    char *tag_start, *tag_end;
    char* found_connect;
    int line_len, n;
    unsigned long start;
    
    fd = open("AT:", O_RDWR);
    if (fd < 0) {
        printf("Error: Modem not found.\n");
        return;
    }
    
    printf("Modem connected.\n");
    
    /* Reset and Config */
    modem_send(fd, "ATZ\r\n");
    modem_read(fd, g_buffer, 256, 500); /* Drain */
    
    modem_send(fd, "ATE0\r\n");
    modem_read(fd, g_buffer, 256, 500); /* Drain */

    printf("Connecting to weatherpi.home.arpa:80...\n");
    modem_send(fd, "ATDweatherpi.home.arpa:80\r\n");

    /* Wait for CONNECT */
    found_connect = NULL;
    start = clock();
    while ((clock() - start) < 5000) {
        line_len = 0;
        while (line_len < 63 && (clock() - start) < 5000) {
            char ch;
            n = modem_read_char(fd, &ch, 50);
            if (n == 1) {
                if (ch == '\n' || ch == '\r') {
                    g_temp[line_len] = '\0';
                    break;
                }
                g_temp[line_len++] = ch;
            }
        }
        g_temp[line_len] = '\0';
        
        if (line_len > 0) {
            if (strstr(g_temp, "CONNECT") != NULL) {
                found_connect = g_temp;
                break;
            }
            if (strstr(g_temp, "NO CARRIER") != NULL) break;
        }
    }

    if (found_connect == NULL) {
        printf("Connection failed.\n");
        close(fd);
        return;
    }

    printf("Connected. Fetching RSS...\n");

    modem_send(fd, "GET /weewx/rss.xml HTTP/1.1\r\n");
    modem_send(fd, "Host: weatherpi.home.arpa\r\n");
    modem_send(fd, "Connection: close\r\n\r\n");
    
    /* Read Response */
    printf("Downloading...\n");
    bytes_read = modem_read(fd, g_buffer, BUFFER_SIZE - 1, 3000);
    g_buffer[bytes_read] = '\0';
    
    printf("Received %d bytes.\n", bytes_read);

    /* Parse for description */
    printf("Parsing...\n");
    pos = 0;
    n = 0;
    
    while (pos < bytes_read) {
        tag_start = strstr(g_buffer + pos, "<description>");
        if (tag_start == NULL) break;
        
        tag_start += 13; /* Len of <description> */
        tag_end = strstr(tag_start, "</description>");
        if (tag_end == NULL) break;
        
        *tag_end = '\0';
        // printf("- %s\n", tag_start);
        print_clean(tag_start);
        n++;
        
        pos = (int)(tag_end - g_buffer) + 14; 
    }
    
    if (n == 0) {
        printf("No <description> tags found.\n");
        /* Optional: Print start of buffer to debug if still failing */
        printf("Buffer start: %.60s\n", g_buffer);
    }
    
    close(fd);
}

void main(void)
{
    /* Clear screen and print Header */
    printf(ANSI_CLS);
    printf(ANSI_BOLD ANSI_CYAN "========================================\n");
    printf("       RP6502 WEATHER STATION           \n");
    printf("========================================\n" ANSI_RESET);
    
    fetch_weather_rss();
    
    printf(ANSI_BOLD ANSI_CYAN "\n========================================\n");
    printf("               DONE.                    \n");
    printf("========================================\n" ANSI_RESET);
}