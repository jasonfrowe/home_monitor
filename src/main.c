#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

/* cc65 prototypes */
char *strstr(const char *haystack, const char *needle);

#ifndef RIA_OP_READ_XSTACK
#define RIA_OP_READ_XSTACK 0x0B
#endif

/* Increased to 8KB to ensure we get the XML body after headers */
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

/* Send string (Reversed for Stack LIFO) */
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

/* Bulk read */
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
            for (i = count - 1; i >= 0; i--) {
                g_read_temp[i] = ria_pop_char();
            }
            for (i = 0; i < count; i++) {
                buf[total++] = g_read_temp[i];
            }
            start = clock(); /* Reset timeout on activity */
        }
    }
    return total;
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
            /* printf("M: %s\n", g_temp); */ /* Optional verbose log */
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
    /* Close is important so server terminates stream */
    modem_send(fd, "Connection: close\r\n\r\n");
    
    /* Read Response */
    printf("Downloading...\n");
    bytes_read = modem_read(fd, g_buffer, BUFFER_SIZE - 1, 3000);
    g_buffer[bytes_read] = '\0';
    
    printf("Received %d bytes.\n", bytes_read);
    
    /* --- DEBUG SECTION START --- */
    printf("\n[DEBUG] Header info:\n");
    /* Print first 100 chars to verify HTTP 200 OK */
    for(n=0; n<100 && n<bytes_read; n++) {
        char c = g_buffer[n];
        /* Replace newlines with space for compact printing */
        if (c == '\r' || c == '\n') putchar(' ');
        else if (c >= 32 && c <= 126) putchar(c);
        else putchar('.');
    }
    printf("\n[DEBUG] End Header info\n\n");
    /* --- DEBUG SECTION END --- */

    if (bytes_read >= BUFFER_SIZE - 1) {
        printf("Warning: Buffer full. XML might be truncated.\n");
    }

    /* Parse for description */
    printf("Parsing...\n");
    pos = 0;
    n = 0; /* Count items found */
    
    while (pos < bytes_read) {
        tag_start = strstr(g_buffer + pos, "<description>");
        if (tag_start == NULL) break;
        
        tag_start += 13; /* Len of <description> */
        tag_end = strstr(tag_start, "</description>");
        if (tag_end == NULL) break;
        
        *tag_end = '\0';
        printf("- %s\n", tag_start);
        n++;
        
        pos = (int)(tag_end - g_buffer) + 14; 
    }
    
    if (n == 0) {
        printf("No <description> tags found.\n");
        /* Check if we got a 404 or redirect by looking for Title */
        tag_start = strstr(g_buffer, "<title>");
        if (tag_start) {
             tag_start += 7;
             tag_end = strstr(tag_start, "</title>");
             if(tag_end) {
                 *tag_end = '\0';
                 printf("Found page title instead: %s\n", tag_start);
             }
        }
    }
    
    close(fd);
}

void main(void)
{
    printf("\nWeather Monitor\n");
    fetch_weather_rss();
    printf("\nDone.\n");
}