#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <SDL/SDL.h>
#include "serial_hal.h"
#include "font.h" 
#include "cursor_pusher.h" // 引入小人推光标模块
#include "audio_player.h"  // 引入音频模块

// --- 基础配置 ---
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define SERIAL_PORT   "/dev/ttyACM0" 

// --- 界面参数 ---
#define GRID_SIZE     30
#define CENTER_X      (SCREEN_WIDTH / 2)
#define CENTER_Y      (SCREEN_HEIGHT / 2)
#define MEASURE_WIN_W   80
#define MEASURE_WIN_H   82
#define MEASURE_WIN_X   (SCREEN_WIDTH - MEASURE_WIN_W - 2)
#define MEASURE_WIN_Y   2
#define MEASURE_WIN_ALPHA 128 // 测量窗口背景透明度 (0:全透 - 255:不透)

// --- 颜色定义 ---
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define COLOR_BG        RGB565(50, 50, 50)       
#define COLOR_GRID      RGB565(60, 60, 60)    
#define COLOR_AXIS      RGB565(120, 120, 120) 
#define COLOR_WAVE      RGB565(0, 255, 0)     
#define COLOR_TEXT      RGB565(255, 255, 255) 
#define COLOR_BAR_BG    RGB565(30, 30, 30)    
#define COLOR_STATUS_OK RGB565(0, 255, 0)     
#define COLOR_STATUS_NO RGB565(255, 0, 0)
#define COLOR_STATUS_PAUSE RGB565(255, 255, 0)  
#define COLOR_CURSOR    RGB565(255, 255, 0)   
#define COLOR_CURSOR_SEL RGB565(255, 0, 0)    
#define COLOR_OVERLAY   RGB565(20, 20, 40)    
#define COLOR_ALERT_BG  RGB565(50, 0, 0)      
#define COLOR_ZERO_LINE RGB565(0, 100, 255)
#define COLOR_LOAD_TRAIL RGB565(0, 200, 255) 

// --- 状态结构 ---
typedef struct {
    int paused;             
    int show_measure;       
    int volt_div_idx;       
    int time_div_idx;       
    int cursor_x1, cursor_x2;
    int cursor_y1, cursor_y2;
    int active_cursor;      
    int show_exit_dialog;   
    int start_pressed;      
    Uint32 start_press_time;
    int start_handled;
    int zero_pos_y;         
} AppState;

float VOLT_PER_DIV[] = {0.5f, 1.0f, 2.0f, 5.0f}; 
const char* VOLT_DIV_STRS[] = {"0.5V", "1.0V", "2.0V", "5.0V"};
const int VOLT_LEVELS = 4;

float TIME_PER_DIV[] = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f};
const char* TIME_DIV_STRS[] = {"500us", "1ms", "2ms", "5ms", "10ms", "20ms", "50ms", "100ms", "200ms", "500ms"};
const int TIME_LEVELS = 10;

int data_buffer[SCREEN_WIDTH]; 
int serial_fd = -1;
Uint32 last_packet_time = 0;   

AppState state = {
    0, 0, 
    1, 1, 
    CENTER_X - 50, CENTER_X + 50, 
    CENTER_Y - 40, CENTER_Y + 40,
    0,
    0, 0, 0, 0,
    CENTER_Y 
};

// --- 函数前向声明 ---
float pixel_to_time(int x);
float pixel_to_volt(int y);

// --- 绘图函数 ---
void put_pixel(SDL_Surface* screen, int x, int y, Uint16 color) {
    if(x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        Uint16 *pixels = (Uint16 *)screen->pixels;
        pixels[y * (screen->pitch / 2) + x] = color;
    }
}

void draw_char(SDL_Surface* screen, int x, int y, char c, Uint16 color) {
    if (c < 32 || c > 122) c = 32; 
    const unsigned char* bitmap = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (bitmap[col] & (1 << row)) put_pixel(screen, x + col, y + row, color);
        }
    }
}

void draw_string(SDL_Surface* screen, int x, int y, const char* str, Uint16 color) {
    while (*str) { draw_char(screen, x, y, *str, color); x += 6; str++; }
}

void draw_text_f(SDL_Surface* screen, int x, int y, Uint16 color, const char* fmt, ...) {
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    draw_string(screen, x, y, buf, color);
}

void draw_dotted_v(SDL_Surface* screen, int x, Uint16 color) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) if (y % 4 < 2) put_pixel(screen, x, y, color);
}
void draw_dotted_h(SDL_Surface* screen, int y, Uint16 color) {
    for (int x = 0; x < SCREEN_WIDTH; x++) if (x % 4 < 2) put_pixel(screen, x, y, color);
}
void draw_zero_arrow(SDL_Surface* screen, int y, Uint16 color) {
    for (int h = 0; h <= 4; h++) {
        int width = 8 - (h * 2); 
        if (y - h >= 0 && y - h < SCREEN_HEIGHT) {
            for (int x = 0; x < width; x++) put_pixel(screen, x, y - h, color);
        }
        if (h > 0 && y + h >= 0 && y + h < SCREEN_HEIGHT) {
            for (int x = 0; x < width; x++) put_pixel(screen, x, y + h, color);
        }
    }
}
void draw_grid(SDL_Surface* screen) {
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    for (int x = CENTER_X; x < SCREEN_WIDTH; x += GRID_SIZE) draw_dotted_v(screen, x, COLOR_GRID);
    for (int x = CENTER_X; x >= 0; x -= GRID_SIZE) draw_dotted_v(screen, x, COLOR_GRID);
    for (int y = CENTER_Y; y < SCREEN_HEIGHT; y += GRID_SIZE) draw_dotted_h(screen, y, COLOR_GRID);
    for (int y = CENTER_Y; y >= 0; y -= GRID_SIZE) draw_dotted_h(screen, y, COLOR_GRID);
    for (int x = 0; x < SCREEN_WIDTH; x++) put_pixel(screen, x, CENTER_Y, COLOR_AXIS);
    for (int y = 0; y < SCREEN_HEIGHT; y++) put_pixel(screen, CENTER_X, y, COLOR_AXIS);
    
    // 刻度绘制
    float step = GRID_SIZE / 4.0f;
    for (int i = 1; ; i++) {
        int offset = (int)(i * step);
        if (CENTER_X + offset >= SCREEN_WIDTH && CENTER_X - offset < 0) break; 
        int tick_len = (i % 4 == 0) ? 4 : 2; 
        if (CENTER_X + offset < SCREEN_WIDTH) {
            for (int h = -tick_len; h <= tick_len; h++) put_pixel(screen, CENTER_X + offset, CENTER_Y + h, COLOR_AXIS);
        }
        if (CENTER_X - offset >= 0) {
            for (int h = -tick_len; h <= tick_len; h++) put_pixel(screen, CENTER_X - offset, CENTER_Y + h, COLOR_AXIS);
        }
    }
    for (int i = 1; ; i++) {
        int offset = (int)(i * step);
        if (CENTER_Y + offset >= SCREEN_HEIGHT && CENTER_Y - offset < 0) break;
        int tick_len = (i % 4 == 0) ? 4 : 2;
        if (CENTER_Y + offset < SCREEN_HEIGHT) {
            for (int w = -tick_len; w <= tick_len; w++) put_pixel(screen, CENTER_X + w, CENTER_Y + offset, COLOR_AXIS);
        }
        if (CENTER_Y - offset >= 0) {
            for (int w = -tick_len; w <= tick_len; w++) put_pixel(screen, CENTER_X + w, CENTER_Y - offset, COLOR_AXIS);
        }
    }

    if (state.zero_pos_y >= 0 && state.zero_pos_y < SCREEN_HEIGHT) {
        for (int x = 0; x < SCREEN_WIDTH; x++) { if (x % 4 < 2) put_pixel(screen, x, state.zero_pos_y, COLOR_ZERO_LINE); }
        draw_zero_arrow(screen, state.zero_pos_y, COLOR_ZERO_LINE);
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
}

// --- 计算函数实现 ---
float pixel_to_time(int x) {
    float time_per_px = TIME_PER_DIV[state.time_div_idx] / (float)GRID_SIZE;
    return (float)(x - CENTER_X) * time_per_px;
}

float pixel_to_volt(int y) {
    float volt_per_px = VOLT_PER_DIV[state.volt_div_idx] / (float)GRID_SIZE;
    return (float)(state.zero_pos_y - y) * volt_per_px;
}

// --- [新增] 绘制标签辅助函数 ---
// 在指定坐标绘制带背景的小标签
void draw_cursor_tag(SDL_Surface* screen, int x, int y, const char* text, Uint16 bg_color, Uint16 text_color) {
    int w = strlen(text) * 6; // 字体宽5+间隔1
    int h = 8;                // 字体高7+间隔1
    SDL_Rect rect = {x, y, w, h};
    // 绘制标签背景，遮挡下面的网格或波形，使文字更清晰
    SDL_FillRect(screen, &rect, bg_color);
    draw_string(screen, x, y, text, text_color);
}

void draw_measurements(SDL_Surface* screen) {
    if (!state.show_measure) return;
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    Uint16 cx1 = (state.active_cursor==0)?COLOR_CURSOR_SEL:COLOR_CURSOR;
    Uint16 cx2 = (state.active_cursor==1)?COLOR_CURSOR_SEL:COLOR_CURSOR;
    for (int y=0; y<SCREEN_HEIGHT; y+=2) { put_pixel(screen, state.cursor_x1, y, cx1); put_pixel(screen, state.cursor_x2, y, cx2); }
    Uint16 cy1 = (state.active_cursor==2)?COLOR_CURSOR_SEL:COLOR_CURSOR;
    Uint16 cy2 = (state.active_cursor==3)?COLOR_CURSOR_SEL:COLOR_CURSOR;
    for (int x=0; x<SCREEN_WIDTH; x+=2) { put_pixel(screen, x, state.cursor_y1, cy1); put_pixel(screen, x, state.cursor_y2, cy2); }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    
    // --- [修改] 绘制光标标签 (X1/X2 紧贴顶部，Y1/Y2 紧贴左侧) ---
    // X标签: y坐标设为 0 (最顶端)
    draw_cursor_tag(screen, state.cursor_x1 - 6, 0, "X1", COLOR_BG, cx1);
    draw_cursor_tag(screen, state.cursor_x2 - 6, 0, "X2", COLOR_BG, cx2);
    
    // Y标签: x坐标设为 0 (最左端)
    draw_cursor_tag(screen, 0, state.cursor_y1 - 4, "Y1", COLOR_BG, cy1);
    draw_cursor_tag(screen, 0, state.cursor_y2 - 4, "Y2", COLOR_BG, cy2);
    // -----------------------------------------------------

    // --- 绘制半透明数据窗口 ---
    SDL_Rect dst_rect = {MEASURE_WIN_X, MEASURE_WIN_Y, MEASURE_WIN_W, MEASURE_WIN_H};
    SDL_Surface* bg_surf = SDL_CreateRGBSurface(SDL_SWSURFACE, 
                                                MEASURE_WIN_W, MEASURE_WIN_H, 
                                                screen->format->BitsPerPixel,
                                                screen->format->Rmask,
                                                screen->format->Gmask,
                                                screen->format->Bmask,
                                                0);
    if (bg_surf) {
        SDL_FillRect(bg_surf, NULL, COLOR_OVERLAY);
        SDL_SetAlpha(bg_surf, SDL_SRCALPHA, MEASURE_WIN_ALPHA);
        SDL_BlitSurface(bg_surf, NULL, screen, &dst_rect);
        SDL_FreeSurface(bg_surf);
    }

    int tx = MEASURE_WIN_X + 5, ty = MEASURE_WIN_Y + 5;
    float t1 = pixel_to_time(state.cursor_x1); float t2 = pixel_to_time(state.cursor_x2);
    draw_text_f(screen, tx, ty, (state.active_cursor==0)?COLOR_CURSOR_SEL:COLOR_TEXT, "X1: %.2fms", t1);
    draw_text_f(screen, tx, ty+10, (state.active_cursor==1)?COLOR_CURSOR_SEL:COLOR_TEXT, "X2: %.2fms", t2);
    draw_text_f(screen, tx, ty+20, COLOR_TEXT, "dX: %.2fms", t2-t1);
    float v1 = pixel_to_volt(state.cursor_y1); float v2 = pixel_to_volt(state.cursor_y2);
    draw_text_f(screen, tx, ty+38, (state.active_cursor==2)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y1: %.2fV", v1);
    draw_text_f(screen, tx, ty+48, (state.active_cursor==3)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y2: %.2fV", v2);
    draw_text_f(screen, tx, ty+58, COLOR_TEXT, "dY: %.2fV", v2-v1);
}

void draw_exit_dialog(SDL_Surface* screen) {
    if (!state.show_exit_dialog) return;
    SDL_Rect rect = {CENTER_X - 80, CENTER_Y - 30, 160, 60};
    SDL_FillRect(screen, &rect, COLOR_ALERT_BG);
    draw_string(screen, rect.x + 35, rect.y + 15, "EXIT APP?", COLOR_TEXT);
    draw_string(screen, rect.x + 20, rect.y + 35, "Press A to Confirm", COLOR_TEXT);
}

void draw_ui(SDL_Surface* screen, int connected) {
    SDL_FillRect(screen, NULL, COLOR_BG);
    draw_grid(screen); 
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    float mv_per_div = VOLT_PER_DIV[state.volt_div_idx] * 1000.0f;
    float pixels_per_mv = (float)GRID_SIZE / mv_per_div;
    for (int x = 0; x < SCREEN_WIDTH - 1; x++) {
        int mv_val = data_buffer[x];
        int mv_next = data_buffer[x+1];
        int scaled_y = state.zero_pos_y - (int)(mv_val * pixels_per_mv);
        int scaled_next = state.zero_pos_y - (int)(mv_next * pixels_per_mv);
        if (scaled_y >= 0 && scaled_y < SCREEN_HEIGHT) {
            put_pixel(screen, x, scaled_y, COLOR_WAVE);
            if (abs(scaled_next - scaled_y) > 1 && abs(scaled_next - scaled_y) < SCREEN_HEIGHT) {
                int step = (scaled_next > scaled_y) ? 1 : -1;
                for (int k = scaled_y; k != scaled_next; k += step) if (k>=0 && k<SCREEN_HEIGHT) put_pixel(screen, x, k, COLOR_WAVE);
            }
        }
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    
    draw_measurements(screen);
    
    if (state.show_measure) {
        Pusher_Render(screen);
    }

    draw_exit_dialog(screen);
    SDL_Rect bar = {0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20}; SDL_FillRect(screen, &bar, COLOR_BAR_BG);
    Uint16 stat_color = COLOR_STATUS_NO;
    if (state.paused) stat_color = COLOR_STATUS_PAUSE; else if (connected) stat_color = COLOR_STATUS_OK;
    SDL_Rect stat = {5, SCREEN_HEIGHT - 14, 8, 8}; SDL_FillRect(screen, &stat, stat_color);
    draw_text_f(screen, 20, SCREEN_HEIGHT - 13, COLOR_TEXT, "Time:%s", TIME_DIV_STRS[state.time_div_idx]);
    draw_text_f(screen, 100, SCREEN_HEIGHT - 13, COLOR_TEXT, "Volt:%s", VOLT_DIV_STRS[state.volt_div_idx]);
    draw_text_f(screen, 220, SCREEN_HEIGHT - 13, COLOR_TEXT, state.show_measure ? "[MEASURE]" : "[VIEW]");
}

void send_timebase_command(int idx) {
    if (serial_fd == -1) return;
    char cmd_buf[32];
    int len = snprintf(cmd_buf, sizeof(cmd_buf), "TIM:%d\n", idx);
    if (write(serial_fd, cmd_buf, len) != len) {
    }
    for (int i = 0; i < SCREEN_WIDTH; i++) data_buffer[i] = 0;
}

#define FRAME_HEADER_SIZE 2
#define DATA_POINTS       SCREEN_WIDTH
#define FRAME_DATA_SIZE   (DATA_POINTS * 2)
#define FRAME_SIZE        (FRAME_HEADER_SIZE + FRAME_DATA_SIZE)

uint8_t rx_buffer[FRAME_SIZE * 2]; 
int rx_len = 0;

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return 1;
    SDL_ShowCursor(SDL_DISABLE); 
    SDL_EnableKeyRepeat(300, 30);
    SDL_Surface* screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE);
    
    if (Audio_Init("key_sound.wav") != 0) {
        printf("Warning: Audio init failed. Ensure key_sound.wav exists.\n");
    }

    if (Pusher_Init() != 0) {
        printf("Pusher Init Failed (check walk.bmp)\n");
    }

    int running = 1;
    for (int i = 0; i < SCREEN_WIDTH; i++) data_buffer[i] = 0;

    Uint8 key_press_flags[SDLK_LAST];
    memset(key_press_flags, 0, sizeof(key_press_flags));

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym < SDLK_LAST && key_press_flags[event.key.keysym.sym] == 0) {
                    Audio_Play();
                    key_press_flags[event.key.keysym.sym] = 1;
                }
            } else if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym < SDLK_LAST) {
                    key_press_flags[event.key.keysym.sym] = 0;
                }
            }

            if (state.show_exit_dialog) {
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_LALT) running = 0; 
                    else {
                        state.show_exit_dialog = 0;
                        state.start_handled = 1;
                    }
                }
                continue; 
            }

            if (event.type == SDL_KEYDOWN) {
                int key = event.key.keysym.sym;
                if (key == SDLK_q) running = 0;
                if (key == SDLK_RETURN) {
                    if (!state.start_pressed) {
                        state.start_pressed = 1;
                        state.start_press_time = SDL_GetTicks();
                        state.start_handled = 0;
                    }
                }
                if (key == SDLK_ESCAPE) state.show_measure = !state.show_measure;

                if (key == SDLK_LCTRL) { 
                     state.time_div_idx = (state.time_div_idx + 1) % TIME_LEVELS;
                     send_timebase_command(state.time_div_idx);
                }
                else if (key == SDLK_LALT) {
                     state.time_div_idx = (state.time_div_idx - 1 + TIME_LEVELS) % TIME_LEVELS;
                     send_timebase_command(state.time_div_idx);
                }

                if (key == SDLK_SPACE) state.volt_div_idx = (state.volt_div_idx + 1) % VOLT_LEVELS;
                else if (key == SDLK_LSHIFT) state.volt_div_idx = (state.volt_div_idx - 1 + VOLT_LEVELS) % VOLT_LEVELS;

                if (state.show_measure) {
                    if (key == SDLK_TAB || key == SDLK_BACKSPACE) state.active_cursor = (state.active_cursor + 1) % 4;
                    int* target = NULL;
                    int step = 1;
                    
                    int moved = 0;      
                    int move_delta = 0; 
                    CursorType move_type = CURSOR_TYPE_X; 

                    if (state.active_cursor == 0) { target = &state.cursor_x1; move_type = CURSOR_TYPE_X; }
                    else if (state.active_cursor == 1) { target = &state.cursor_x2; move_type = CURSOR_TYPE_X; }
                    else if (state.active_cursor == 2) { target = &state.cursor_y1; move_type = CURSOR_TYPE_Y; }
                    else if (state.active_cursor == 3) { target = &state.cursor_y2; move_type = CURSOR_TYPE_Y; }

                    if (key == SDLK_UP && (state.active_cursor >= 2)) { 
                        *target -= step; moved = 1; move_delta = -step;
                    }
                    if (key == SDLK_DOWN && (state.active_cursor >= 2)) { 
                        *target += step; moved = 1; move_delta = step;
                    }
                    if (key == SDLK_LEFT && (state.active_cursor < 2)) { 
                        *target -= step; moved = 1; move_delta = -step;
                    }
                    if (key == SDLK_RIGHT && (state.active_cursor < 2)) { 
                        *target += step; moved = 1; move_delta = step;
                    }
                    
                    if (moved) {
                        Pusher_OnMove(move_type, *target, move_delta);
                    }
                } 
                else {
                    if (key == SDLK_UP) state.zero_pos_y -= 5;
                    else if (key == SDLK_DOWN) state.zero_pos_y += 5;
                }
            }
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_RETURN) {
                    if (state.start_pressed) {
                        if (!state.start_handled) state.paused = !state.paused;
                        state.start_pressed = 0;
                    }
                }
            }
        }

        if (state.start_pressed && !state.show_exit_dialog) {
            if (SDL_GetTicks() - state.start_press_time > 2000) { 
                state.show_exit_dialog = 1;
                state.start_handled = 1;
            }
        }

        if (serial_fd == -1) {
            serial_fd = serial_open(SERIAL_PORT);
            if (serial_fd != -1) send_timebase_command(state.time_div_idx);
        }

        if (!state.paused && serial_fd != -1) {
            int n = serial_read_bytes(serial_fd, rx_buffer + rx_len, sizeof(rx_buffer) - rx_len);
            if (n > 0) {
                rx_len += n;
                last_packet_time = SDL_GetTicks(); 
            }
            while (rx_len >= FRAME_SIZE) {
                if (rx_buffer[0] == 0xFA && rx_buffer[1] == 0xFB) {
                    uint8_t* p = rx_buffer + FRAME_HEADER_SIZE;
                    for (int i = 0; i < SCREEN_WIDTH; i++) {
                        uint16_t val = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                        data_buffer[i] = (int)val;
                        p += 2;
                    }
                    int remaining = rx_len - FRAME_SIZE;
                    if (remaining > 0) memmove(rx_buffer, rx_buffer + FRAME_SIZE, remaining);
                    rx_len = remaining;
                } else {
                    memmove(rx_buffer, rx_buffer + 1, rx_len - 1);
                    rx_len--;
                }
            }
        }
        
        int connected = 0;
        if (serial_fd != -1) {
            if (SDL_GetTicks() - last_packet_time < 200) connected = 1;
        }

        draw_ui(screen, connected);
        SDL_Flip(screen);
        SDL_Delay(10);
    }
    
    Pusher_Cleanup();
    Audio_Cleanup();
    
    if (serial_fd != -1) serial_close(serial_fd);
    SDL_Quit();
    return 0;
}
