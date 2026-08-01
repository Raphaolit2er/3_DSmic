#include <nds.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// ----- Configuration -----
char serverIP[16] = "192.168.1.100";
int serverPort = 8888;

// ----- Audio parameters (Dynamic via PC) -----
int current_sample_rate = 0;
bool got_initial_settings = false;

// Dynamic buffer tracking instead of hardcoded macros
int current_frame_bytes = 0;

static uint8_t hwMicBuffer[8192];  // Large enough to hold max MTU payloads safely
static int16_t audioBuffer[4096];  // Matches max hardware buffer
volatile bool micFrameReady = false;

// ----- UI Framework -----
u16 backBottom[256 * 192] __attribute__((aligned(4)));
int bgBottom = -1;

const u8* getGlyph(char ch) {
    static const u8 glyph_0[] = {0x3E,0x51,0x49,0x45,0x3E};
    static const u8 glyph_1[] = {0x00,0x42,0x7F,0x40,0x00};
    static const u8 glyph_2[] = {0x42,0x61,0x51,0x49,0x46};
    static const u8 glyph_3[] = {0x21,0x41,0x45,0x4B,0x31};
    static const u8 glyph_4[] = {0x18,0x14,0x12,0x7F,0x10};
    static const u8 glyph_5[] = {0x27,0x45,0x45,0x45,0x39};
    static const u8 glyph_6[] = {0x3C,0x4A,0x49,0x49,0x30};
    static const u8 glyph_7[] = {0x01,0x71,0x09,0x05,0x03};
    static const u8 glyph_8[] = {0x36,0x49,0x49,0x49,0x36};
    static const u8 glyph_9[] = {0x06,0x49,0x49,0x29,0x1E};
    
    static const u8 glyph_less[]  = {0x00,0x08,0x14,0x22,0x41};
    static const u8 glyph_minus[] = {0x08,0x08,0x08,0x08,0x08};
    static const u8 glyph_E[] = {0x7F,0x49,0x49,0x49,0x41};
    static const u8 glyph_N[] = {0x7F,0x04,0x08,0x10,0x7F};
    static const u8 glyph_T[] = {0x01,0x01,0x7F,0x01,0x01};
    static const u8 glyph_blank[] = {0,0,0,0,0};

    switch(ch) {
        case '0': return glyph_0; case '1': return glyph_1;
        case '2': return glyph_2; case '3': return glyph_3;
        case '4': return glyph_4; case '5': return glyph_5;
        case '6': return glyph_6; case '7': return glyph_7;
        case '8': return glyph_8; case '9': return glyph_9;
        case '<': return glyph_less; case '-': return glyph_minus;
        case 'E': return glyph_E; case 'N': return glyph_N;
        case 'T': return glyph_T; default:  return glyph_blank;
    }
}

#define KB_ROWS 4
#define KB_COLS 3
const char kb_keys[KB_ROWS][KB_COLS] = {
    {'7', '8', '9'}, {'4', '5', '6'},
    {'1', '2', '3'}, {'0', '\b', '\n'}
};
int kb_cx = 0, kb_cy = 0; bool kb_visible = false;

void swapBottom(void) {
    if (bgBottom != -1) dmaCopyHalfWords(3, backBottom, bgGetGfxPtr(bgBottom), 256 * 192 * sizeof(u16));
}
void drawKeyboardToBuffer(void) {
    if (!kb_visible) return;
    for (int i = 0; i < 256*192; i++) backBottom[i] = RGB15(3,3,3)|BIT(15);
    for (int row = 0; row < KB_ROWS; row++) {
        for (int col = 0; col < KB_COLS; col++) {
            int kx = 40 + col*60, ky = 96 + row*24, kw = 56, kh = 20;
            u16 bg = (kb_cy==row && kb_cx==col) ? RGB15(31,0,0)|BIT(15) : RGB15(12,12,12)|BIT(15);
            for (int py=0; py<kh; py++)
                for (int px=0; px<kw; px++) backBottom[(ky+py)*256 + (kx+px)] = bg;
            
            char label[4]; char c = kb_keys[row][col];
            if (c == '\b') strcpy(label, "<-"); 
            else if (c == '\n') strcpy(label, "ENT"); 
            else { label[0]=c; label[1]='\0'; }
            
            int textLen = strlen(label); int textPx = kx + (kw - textLen*6)/2 + 1; int textPy = ky + 6;
            for (int i=0; i<textLen; i++) {
                const u8* glyph = getGlyph(label[i]);
                for (int gx=0; gx<5; gx++) {
                    u8 colBits = glyph[gx];
                    for (int gy=0; gy<7; gy++) if ((colBits >> gy) & 1) backBottom[(textPy+gy)*256 + (textPx+gx)] = RGB15(31,31,31)|BIT(15);
                }
                textPx += 6;
            }
        }
    }
}
void refreshKeyboard(void) { drawKeyboardToBuffer(); swapBottom(); }
void showCustomKeyboard(void) { kb_cx=0; kb_cy=0; kb_visible=true; refreshKeyboard(); }
void hideCustomKeyboard(void) { kb_visible=false; dmaFillHalfWords(0x8000, backBottom, sizeof(backBottom)); swapBottom(); }

char updateCustomKeyboard(u32 down, u32 repeat) {
    bool changed = false;
    if (repeat & KEY_UP)    { kb_cy--; if (kb_cy<0) kb_cy=KB_ROWS-1; changed=true; }
    if (repeat & KEY_DOWN)  { kb_cy++; if (kb_cy>=KB_ROWS) kb_cy=0; changed=true; }
    if (repeat & KEY_LEFT)  { kb_cx--; if (kb_cx<0) kb_cx=KB_COLS-1; changed=true; }
    if (repeat & KEY_RIGHT) { kb_cx++; if (kb_cx>=KB_COLS) kb_cx=0; changed=true; }
    if (down & KEY_TOUCH) {
        touchPosition touch; touchRead(&touch);
        if (touch.py >= 96 && touch.px >= 8) {
            int gx = (touch.px - 40) / 60; int gy = (touch.py - 96) / 24;
            if (gx>=0 && gx<KB_COLS && gy>=0 && gy<KB_ROWS) { kb_cx = gx; kb_cy = gy; down |= KEY_A; changed = true; }
        }
    }
    if (changed) refreshKeyboard();
    if (down & KEY_A) return kb_keys[kb_cy][kb_cx];
    return 0;
}
int getPortInput(void) {
    showCustomKeyboard();
    char buf[8] = {0};
    int len = 0;

    iprintf("\x1b[2JEnter Port (digits, ENT to confirm):\n\n> ");
    while (1) {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown(), repeat = keysDownRepeat();
        char c = updateCustomKeyboard(down, repeat);

        if (c == '\n') break;
        else if (c == '\b') {
            if (len > 0) {
                len--;
                buf[len] = '\0';
            }
        } else if (c >= '0' && c <= '9' && len < 7) {
            buf[len++] = c;
            buf[len] = '\0';
        }

        // Refresh the top screen with the current buffer
        iprintf("\x1b[1;0H> %-7s   ", buf);  // overwrite line

        if (down & KEY_SELECT) {
            len = 0;
            buf[0] = '\0';
            break;
        }
    }
    hideCustomKeyboard();
    return (len > 0) ? atoi(buf) : -1;
}
void editIP(void) {
    int octets[4] = {0}; int cursor=0;
    sscanf(serverIP, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);
    int holdFrames=0; const int HOLD_DELAY=20, REPEAT_RATE=3; consoleClear();
    while (1) {
        printf("\x1b[0;0HSet Server IP (D-Pad, A=OK):\n");
        for (int i=0; i<4; i++) { if (i==cursor) printf("[%3d]", octets[i]); else printf(" %3d ", octets[i]); if (i<3) printf("."); }
        printf("\n\nHold UP/DOWN to change\nA: confirm IP");
        swiWaitForVBlank(); scanKeys(); u32 held = keysHeld(), down = keysDown();
        if (down & KEY_RIGHT && cursor<3) cursor++;
        if (down & KEY_LEFT  && cursor>0) cursor--;
        if (held & KEY_UP) {
            if (down & KEY_UP) { octets[cursor]++; if (octets[cursor]>255) octets[cursor]=0; holdFrames=0; }
            else { holdFrames++; if (holdFrames>=HOLD_DELAY && (holdFrames-HOLD_DELAY)%REPEAT_RATE==0) { octets[cursor]++; if (octets[cursor]>255) octets[cursor]=0; } }
        } else if (held & KEY_DOWN) {
            if (down & KEY_DOWN) { octets[cursor]--; if (octets[cursor]<0) octets[cursor]=255; holdFrames=0; }
            else { holdFrames++; if (holdFrames>=HOLD_DELAY && (holdFrames-HOLD_DELAY)%REPEAT_RATE==0) { octets[cursor]--; if (octets[cursor]<0) octets[cursor]=255; } }
        } else { holdFrames=0; }
        if (down & KEY_A) break;
    }
    sprintf(serverIP, "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]); consoleClear();
}

// ----- Mic callback -----
void micCallback(void *buffer, int length) {
    memcpy(audioBuffer, buffer, length);
    micFrameReady = true;
}

// ----- Main -----
int main(void) {
    videoSetMode(MODE_0_2D); vramSetBankA(VRAM_A_MAIN_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    videoSetModeSub(MODE_5_2D); vramSetBankC(VRAM_C_SUB_BG);
    bgBottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0); 
    REG_DISPCNT_SUB = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    dmaFillHalfWords(0x8000, backBottom, sizeof(backBottom)); swapBottom();

    iprintf("Set Server IP (D-Pad, A=OK)\n"); editIP();
    iprintf("\x1b[2JSet Server Port (numeric keyboard)\n");
    int port = getPortInput(); if (port > 0 && port < 65536) serverPort = port;
    
    iprintf("\x1b[2J--- Target Config ---\nIP:   %s\nPort: %d\n\n", serverIP, serverPort);

    iprintf("Connecting to AP...\n");
    if (!Wifi_InitDefault(WFC_CONNECT)) { iprintf("WiFi init failed!\n"); while(1) swiWaitForVBlank(); }
    while (1) {
        int st = Wifi_AssocStatus();
        if (st == ASSOCSTATUS_ASSOCIATED) break;
        if (st == ASSOCSTATUS_CANNOTCONNECT) { iprintf("Cannot connect to AP.\n"); while (1) swiWaitForVBlank(); }
        swiWaitForVBlank();
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { iprintf("Socket error\n"); while(1) swiWaitForVBlank(); }
    
    // Explicitly bind the DS to a local port so it can HEAR the PC's reply
    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(serverPort + 1); // Offset the port by 1 for the local side
    my_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr));

    // Make the socket non-blocking so we can check for incoming settings mid-loop
    int block = 1;
    ioctl(sock, FIONBIO, &block);
    
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(serverPort);
    saddr.sin_addr.s_addr = inet_addr(serverIP);
    
    soundEnable();
    const char spinner[] = {'|', '/', '-', '\\'};
    int frameCount = 0;

    while (1) {
        // Safe to sleep here now because the buffer dynamically aligns with ~33ms
        swiWaitForVBlank();
        
        // 1. Check for incoming Settings Packet from PC (0x11) using recvfrom
        uint8_t rx[16];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        
        int rx_len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr*)&from, &from_len);
        
        if (rx_len >= 3 && rx[0] == 0x11) {
            int new_rate = (rx[1] << 8) | rx[2];
            
            // Apply new settings dynamically
            if (new_rate != current_sample_rate) {
                if (current_sample_rate != 0) {
                    soundMicOff(); // Turn off existing stream before applying new
                }
                current_sample_rate = new_rate;
                
                // --- THE DYNAMIC BUFFER CALCULATION ---
                // Calculate samples needed for ~33ms of audio (aligns perfectly with 60Hz screen)
                int target_ms = 33;
                int calculated_samples = (current_sample_rate * target_ms) / 1000;
                
                // Ensure the sample count is an even number for 16-bit memory alignment
                if (calculated_samples % 2 != 0) calculated_samples++;
                
                // Safety Cap: Prevent UDP MTU overflow (>1400 bytes)
                if ((calculated_samples * 2) > 1400) {
                    calculated_samples = 700; // Hard cap
                }
                
                current_frame_bytes = calculated_samples * sizeof(int16_t);
                
                soundMicRecord(hwMicBuffer, current_frame_bytes * 2, MicFormat_12Bit, current_sample_rate, micCallback);
                iprintf("\n> Rate: %d Hz\n> Buffer: %d bytes\n", current_sample_rate, current_frame_bytes);
            }
            got_initial_settings = true;
        }

        // 2. STATE 1: WAITING FOR PC
        if (!got_initial_settings) {
            if (frameCount % 30 == 0) {
                uint8_t ping[1] = {0x10};
                sendto(sock, ping, 1, 0, (struct sockaddr*)&saddr, sizeof(saddr));
                iprintf("\rWaiting for PC settings... %c", spinner[(frameCount/30)%4]);
            }
            frameCount++;
        } 
        
        // 3. STATE 2: NORMAL STREAMING
        else if (micFrameReady) {
            // Allocate a dynamic packet sized for our calculated buffer
            uint8_t packet[1 + 2 + current_frame_bytes];
            packet[0] = 0x20;
            packet[1] = (current_frame_bytes >> 8) & 0xFF;
            packet[2] = current_frame_bytes & 0xFF;
            memcpy(&packet[3], audioBuffer, current_frame_bytes);
            
            sendto(sock, packet, sizeof(packet), 0, (struct sockaddr*)&saddr, sizeof(saddr));
            micFrameReady = false; 
        }
    }

    soundMicOff();
    closesocket(sock);
    return 0;
}