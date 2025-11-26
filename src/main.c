#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
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
#define BUFFER_SIZE 16384
#define UPDATE_INTERVAL_MIN 5
#define TICKS_PER_MIN (60UL * CLOCKS_PER_SEC) 
#define TIMEZONE_OFFSET -5      /* EST = -5, EDT = -4 */
#define SCREEN_WIDTH     78     /* Adjusted to indents */
#define MAX_SCREEN_LINES 22     /* Stop printing after this many lines to prevent scrolling */
#define MAX_FEEDS       10      /* Maximum number of feeds to load */
#define CONFIG_FILENAME "feeds.txt"

/* --- Keyboard / XRAM Configuration --- */
#define KEYBOARD_INPUT  0xEC20  // XRAM address for keyboard data
#define KEYBOARD_BYTES  32      // 32 bytes for 256 key states
#define KEY_ESC         0x29   
#define KEY_ENTER       0x28    
#define KEY_1           0x1E    /* '1' on main row */
#define KEY_2           0x1F    /* '2' on main row */
#define KEY_3           0x20    /* '3' on main row */
#define KEY_4           0x21    /* '4' on main row */
#define KEY_5           0x22    /* '5' on main row */

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
#define ANSI_RED        "\x1b[31m"

/* --- Globals --- */
static char g_buffer[BUFFER_SIZE];
static char g_read_temp[256];
static char g_temp_line[128];
uint8_t keystates[KEYBOARD_BYTES] = {0};

/* --- Helper Functions --- */
static int is_entity(const char* p, const char* entity) {
    return strncmp(p, entity, strlen(entity)) == 0;
}

/* 
 * Custom sleep function since 'sleep()' is missing in the library 
 */
static void wait_seconds(unsigned int sec) {
    unsigned long start = clock();
    unsigned long duration = (unsigned long)sec * CLOCKS_PER_SEC;
    while ((clock() - start) < duration);
}

/* Feed Configuration Structure */
typedef struct {
    int type;           /* 0 = Weather Logic, 1 = News Logic */
    char name[32];
    char host[64];
    char port_str[8]; 
    char path[64];
    char tag[32];       /* <title> or <description> */
    char end_tag[32];   /* </title> or </description> */
    int skip_first;     
} FeedConfig;

/* Global Feed Storage */
FeedConfig feeds[MAX_FEEDS];
int feed_count = 0;
FeedConfig current_feed;

/* 
 * Load Config from File
 * Format: Type|Name|Host|Port|Path|Tag|EndTag|SkipFirst
 */
static void load_config(void) {
    FILE *fp;
    char *token;
    int i = 0;

    fp = fopen(CONFIG_FILENAME, "r");
    if (fp == NULL) {
        printf(ANSI_RED "Error: Could not open %s\n" ANSI_RESET, CONFIG_FILENAME);
        printf("Using defaults...\n");
        /* Fallback Default */
        feeds[0].type = 1;
        strcpy(feeds[0].name, "Slashdot (Default)");
        strcpy(feeds[0].host, "rss.slashdot.org");
        strcpy(feeds[0].port_str, "80");
        strcpy(feeds[0].path, "/Slashdot/slashdot");
        strcpy(feeds[0].tag, "<title>");
        strcpy(feeds[0].end_tag, "</title>");
        feeds[0].skip_first = 1;
        feed_count = 1;
        
        /* CHANGED: sleep(2) -> wait_seconds(2) */
        wait_seconds(2);
        return;
    }

    printf("Loading feeds...\n");
    while (fgets(g_temp_line, sizeof(g_temp_line), fp) != NULL) {
        /* Skip comments or empty lines */
        if (g_temp_line[0] == '#' || g_temp_line[0] == '\n' || strlen(g_temp_line) < 5) continue;
        
        if (feed_count >= MAX_FEEDS) break;
        
        /* Remove trailing newline */
        g_temp_line[strcspn(g_temp_line, "\r\n")] = 0;

        /* Parse Tokens (Pipe Separated) */
        token = strtok(g_temp_line, "|");
        if (!token) continue;
        feeds[feed_count].type = atoi(token);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].name, token, 31);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].host, token, 63);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].port_str, token, 7);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].path, token, 63);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].tag, token, 31);

        token = strtok(NULL, "|"); if (!token) continue;
        strncpy(feeds[feed_count].end_tag, token, 31);

        token = strtok(NULL, "|"); if (!token) continue;
        feeds[feed_count].skip_first = atoi(token);

        printf("Loaded: %s\n", feeds[feed_count].name);
        feed_count++;
    }
    fclose(fp);
    
    /* CHANGED: sleep(1) -> wait_seconds(1) */
    wait_seconds(1);
}

/* 
 * Calculate the visual length of the next word in the buffer 
 * Handles HTML entity lengths (e.g., &quot; is 1 char)
 */
static int get_next_word_len(const char* p, const char* end) {
    int len = 0;
    int in_tag = 0;
    const char* t = p;
    
    while (t < end) {
        /* Stop at space (word boundary) unless inside a tag */
        if (!in_tag && (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t')) break;

        /* Skip CDATA markers in length calculation */
        if (strncmp(t, "<!--", 4) == 0) { in_tag = 1; t+=4; continue; } 
        if (strncmp(t, "<![CDATA[", 9) == 0) { t += 9; continue; }
        if (strncmp(t, "]]>", 3) == 0) { t += 3; continue; }
        
        if (*t == '<') { in_tag = 1; t++; continue; }
        if (*t == '>') { in_tag = 0; t++; continue; }
        if (in_tag) { t++; continue; }

        /* Handle Degree Symbol (UTF-8: 0xC2 0xB0) */
        if ((unsigned char)*t == 0xC2 && (unsigned char)*(t+1) == 0xB0) {
            len += 5; /* " deg " */
            t += 2; 
            continue;
        }

        /* Handle UTF-8 Characters (Visual length = 1) */
        /* Check for 0xE2 marker (common for symbols) */
        if ((unsigned char)*t == 0xE2) {
             /* Em Dash (—) or En Dash (–) */
             if (strncmp(t, "\xE2\x80\x94", 3) == 0 || strncmp(t, "\xE2\x80\x93", 3) == 0) {
                 len += 1; t += 3; continue;
             }
             /* Smart Quotes (“ ” ‘ ’) */
             if (strncmp(t, "\xE2\x80\x9C", 3) == 0 || strncmp(t, "\xE2\x80\x9D", 3) == 0 ||
                 strncmp(t, "\xE2\x80\x98", 3) == 0 || strncmp(t, "\xE2\x80\x99", 3) == 0) {
                 len += 1; t += 3; continue;
             }
        }
        
        if (*t == '&') {
            if (is_entity(t, "&#176;")) { len += 5; t += 6; continue; } /* " deg " */
            if (is_entity(t, "&quot;")) { len += 1; t += 6; continue; }
            if (is_entity(t, "&amp;"))  { len += 1; t += 5; continue; }
            if (is_entity(t, "&lt;"))   { len += 1; t += 4; continue; }
            if (is_entity(t, "&gt;"))   { len += 1; t += 4; continue; }
        }
        
        len++;
        t++;
    }
    return len;
}

static void print_pretty_line(const char* start, const char* end, int max_chars, const char* color, int* current_line, int indent, int is_weather) {
    const char* p = start;
    int is_value = 0;
    int in_tag = 0; 
    int chars_printed = 0;
    int col = indent; /* Assume we started at this column (caller printed prefix) */
    int word_len = 0;
    int i;

    /* Consume initial whitespace */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    if (p >= end) return;

    printf("%s", color);

    while (p < end) {
        if (*current_line >= MAX_SCREEN_LINES) return;
        if (max_chars > 0 && chars_printed >= max_chars) {
            printf(ANSI_RESET "...");
            break;
        }

        /* --- Handle CDATA explicitly BEFORE generic tags --- */
        if (strncmp(p, "<![CDATA[", 9) == 0) {
            p += 9;
            continue;
        }
        if (strncmp(p, "]]>", 3) == 0) {
            p += 3;
            continue;
        }

        /* Handle Tags (Invisible) */
        if (*p == '<') { in_tag = 1; p++; continue; }
        if (*p == '>') { in_tag = 0; p++; continue; }
        if (in_tag) { p++; continue; }

        /* Handle Whitespace */
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (col < SCREEN_WIDTH) {
                putchar(' ');
                col++;
            }
            p++;
            continue;
        }

        /* Start of a word? Check if it fits. */
        word_len = get_next_word_len(p, end);
        
        if (col + word_len > SCREEN_WIDTH) {
            /* Wrap */
            printf("\n");
            (*current_line)++;
            if (*current_line >= MAX_SCREEN_LINES) return;
            
            /* Indent next line */
            for(i=0; i<indent; i++) putchar(' ');
            col = indent;
            
            /* Re-apply color after newline */
            printf("%s", color);
        }

        /* Print the word characters */
        while (p < end && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
             if (*p == '<') { /* Tag start mid-word? stop word processing to let main loop handle tag */ break; }

             /* Stop printing if we hit the end of CDATA */
             if (strncmp(p, "]]>", 3) == 0) break;
             /* Also stop if we hit a standard tag */
             if (*p == '<') break;

             /* Handle Degree Symbol (UTF-8: 0xC2 0xB0) */
             if ((unsigned char)*p == 0xC2 && (unsigned char)*(p+1) == 0xB0) {
                 printf(ANSI_YELLOW " deg %s", color);
                 if (is_value) printf(ANSI_BOLD ANSI_WHITE);
                 p += 2; col += 5; chars_printed++; continue;
             }

             /* UTF-8 Replacements */
             if ((unsigned char)*p == 0xE2) {
                 /* Em Dash (—) or En Dash (–) -> hyphen */
                 if (strncmp(p, "\xE2\x80\x94", 3) == 0 || strncmp(p, "\xE2\x80\x93", 3) == 0) {
                     putchar('-'); p += 3; col++; chars_printed++; continue;
                 }
                 /* Smart Quotes -> " or ' */
                 if (strncmp(p, "\xE2\x80\x9C", 3) == 0 || strncmp(p, "\xE2\x80\x9D", 3) == 0) {
                     putchar('"'); p += 3; col++; chars_printed++; continue;
                 }
                 if (strncmp(p, "\xE2\x80\x98", 3) == 0 || strncmp(p, "\xE2\x80\x99", 3) == 0) {
                     putchar('\''); p += 3; col++; chars_printed++; continue;
                 }
             }
             
             /* Entities */
             if (*p == '&') {
                if (is_entity(p, "&#176;")) {
                    printf(ANSI_YELLOW " deg %s", color);
                    if (is_value) printf(ANSI_BOLD ANSI_WHITE);
                    p += 6; chars_printed++; col+=5; continue;
                }
                if (is_entity(p, "&quot;")) { putchar('"'); p += 6; chars_printed++; col++; continue; }
                if (is_entity(p, "&amp;"))  { putchar('&'); p += 5; chars_printed++; col++; continue; }
                if (is_entity(p, "&lt;"))   { putchar('<'); p += 4; chars_printed++; col++; continue; }
                if (is_entity(p, "&gt;"))   { putchar('>'); p += 4; chars_printed++; col++; continue; }
            }

            /* Weather Semicolons */
            if (is_weather && *p == ';') {
                printf(ANSI_RESET ";\n%s", color); 
                (*current_line)++;
                is_value = 0;
                p++; chars_printed++;
                col = 0; /* Weather lines restart at 0 */
                /* Skip spaces after semi */
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                /* break inner loop to re-eval context */
                break; 
            }

            /* Weather Colons */
            if (is_weather && *p == ':' && !is_value) {
                printf(":%s ", ANSI_RESET);
                is_value = 1;
                printf(ANSI_BOLD ANSI_WHITE);
                p++; chars_printed++; col += 2;
                continue;
            }

            putchar(*p);
            p++;
            col++;
            chars_printed++;
        }
    }
    printf(ANSI_RESET "\n");
    (*current_line)++;
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
    int items_found = 0; 
    int tag_len, end_tag_len;
    int total_lines_printed = 4;
    
    time_t now;
    struct tm *t;
    char time_str[40];

    tag_len = strlen(current_feed.tag);
    end_tag_len = strlen(current_feed.end_tag);

    fd = open("AT:", O_RDWR);
    if (fd < 0) {
        printf(ANSI_MAGENTA "Error: Modem not ready.\n" ANSI_RESET);
        return 0;
    }

    modem_send(fd, "ATZ\r\n");
    modem_read_bulk(fd, g_buffer, 256, 200);
    modem_send(fd, "ATE0\r\n");
    modem_read_bulk(fd, g_buffer, 256, 200);

    printf("Connecting to %s...", current_feed.host);
    
    strcpy(g_temp_line, "ATD");
    strcat(g_temp_line, current_feed.host);
    strcat(g_temp_line, ":");
    strcat(g_temp_line, current_feed.port_str);
    strcat(g_temp_line, "\r\n");
    modem_send(fd, g_temp_line);

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
    
    strcpy(g_temp_line, "GET ");
    strcat(g_temp_line, current_feed.path);
    strcat(g_temp_line, " HTTP/1.1\r\n");
    modem_send(fd, g_temp_line);

    strcpy(g_temp_line, "Host: ");
    strcat(g_temp_line, current_feed.host);
    strcat(g_temp_line, "\r\n");
    modem_send(fd, g_temp_line);
    
    modem_send(fd, "Connection: close\r\n\r\n");

    bytes_read = modem_read_bulk(fd, g_buffer, BUFFER_SIZE - 1, 3000);
    g_buffer[bytes_read] = '\0';
    close(fd);

    now = time(NULL);
    now += (TIMEZONE_OFFSET * 3600); 
    t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%b %d %H:%M", t);

    printf(ANSI_CLS);
    printf(ANSI_GREEN "%s Monitor" ANSI_RESET " (Updated: %s)\n", current_feed.name, time_str);
    printf("----------------------------------------\n\n");

    pos = 0;
    while (pos < bytes_read) {
        if (total_lines_printed >= MAX_SCREEN_LINES) break;

        tag_start = strstr(g_buffer + pos, current_feed.tag);
        if (!tag_start) break;
        tag_start += tag_len; 
        tag_end = strstr(tag_start, current_feed.end_tag);
        if (!tag_end) break;
        *tag_end = '\0'; 
        
        if (current_feed.skip_first && items_found == 0) {
            pos = (int)(tag_end - g_buffer) + end_tag_len;
            items_found++; continue;
        }

        /* Weather Logic */
        if (current_feed.type == 0) {
            if (strstr(tag_start, "summaries") != NULL || strstr(tag_start, "total for month") != NULL || strstr(tag_start, "total for year") != NULL) {
                pos = (int)(tag_end - g_buffer) + end_tag_len; continue;
            }
            if (block_count == 0) { printf(ANSI_YELLOW "CURRENT CONDITIONS:\n" ANSI_RESET); total_lines_printed++; }
            else if (block_count == 1) { printf(ANSI_YELLOW "\nDAILY SUMMARY:\n" ANSI_RESET); total_lines_printed+=2; }
            print_pretty_line(tag_start, tag_end, 2000, ANSI_CYAN, &total_lines_printed, 0, 1); 
        } 
        /* News Logic */
        else {
            printf(ANSI_YELLOW "* ");
            print_pretty_line(tag_start, tag_end, 2000, ANSI_YELLOW, &total_lines_printed, 2, 0);

            {
                char *desc_start = strstr(tag_end + 1, "<description>");
                if (desc_start && (desc_start - tag_end < 500)) {
                    char *desc_end = strstr(desc_start, "</description>");
                    if (desc_end) {
                        *desc_end = '\0';
                        if (total_lines_printed < MAX_SCREEN_LINES) {
                            printf("  "); 
                            print_pretty_line(desc_start + 13, desc_end, 250, ANSI_CYAN, &total_lines_printed, 2, 0); 
                        }
                        *desc_end = '<'; 
                    }
                }
            }
        }
        
        if (current_feed.type != 0) { printf("\n"); total_lines_printed++; }
        block_count++; items_found++;
        pos = (int)(tag_end - g_buffer) + end_tag_len;
    }

    if (items_found == 0) printf(ANSI_RED "No RSS items found.\n" ANSI_RESET);
    return 1;
}


void main(void) {
    unsigned long timer_start;
    unsigned long next_update_ticks = UPDATE_INTERVAL_MIN * TICKS_PER_MIN;
    int running = 1;
    int force_reload = 0;
    int current_idx = 0;
    int i;

    /* Initialize Keyboard */
    xreg(0, 0, 0, KEYBOARD_INPUT);

    printf(ANSI_CLS);
    
    /* Load Config from feeds.txt */
    load_config();
    if (feed_count == 0) {
        printf("No feeds loaded. Exiting.\n");
        return;
    }
    
    /* Start with first feed */
    current_idx = 0;
    current_feed = feeds[current_idx];

    while (running) {
        fetch_data();

        printf(ANSI_YELLOW "\nNext update in %d minutes.\n", UPDATE_INTERVAL_MIN);
        
        /* Dynamic Menu based on loaded feeds */
        for(i=0; i<feed_count && i<9; i++) {
            printf("[%d] %s  ", i+1, feeds[i].name);
            if ((i+1)%4 == 0) printf("\n");
        }
        printf("[ESC] Exit\n" ANSI_RESET);

        timer_start = clock();
        force_reload = 0;

        while ((clock() - timer_start) < next_update_ticks) {
            uint8_t k;
            RIA.addr0 = KEYBOARD_INPUT;
            RIA.step0 = 1;
            for (k = 0; k < KEYBOARD_BYTES; k++) keystates[k] = RIA.rw0;

            if (key(KEY_ESC)) {
                printf("Exiting ...\n");
                xreg(0, 0, 0, 0xFFFF); 
                running = 0;
                break;
            }

            /* Dynamic Key Checking: Keys 1..9 map to feeds 0..8 */
            /* HID Codes: 1=0x1E, 2=0x1F, 3=0x20 ... 9=0x26 */
            for(i=0; i<feed_count && i<9; i++) {
                if (key(0x1E + i)) {
                    if (current_idx != i) { // Note: 'id' in struct isn't strictly used now, we use index
                        current_idx = i;
                        current_feed = feeds[current_idx];
                        printf("\nSwitching to %s...\n", current_feed.name);
                        force_reload = 1;
                        break;
                    }
                }
            }
            
            if (force_reload) break;
        }

        /* Auto-Cycle Logic */
        if (running && !force_reload) {
            current_idx++;
            if (current_idx >= feed_count) current_idx = 0;
            current_feed = feeds[current_idx];
        }
    }
}