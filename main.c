#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <termios.h>
#include <SDL/SDL.h>

// --- 基础配置 ---
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define SERIAL_PORT   "/dev/ttyACM0" 

// --- 界面参数 ---
#define GRID_SIZE     30    // 网格间距 (像素)
#define CENTER_X      (SCREEN_WIDTH / 2)
#define CENTER_Y      (SCREEN_HEIGHT / 2)

// --- 测量窗口布局配置 ---
#define MEASURE_WIN_W   94                          // 窗口宽度
#define MEASURE_WIN_H   82                          // 窗口高度
#define MEASURE_WIN_X   (SCREEN_WIDTH - MEASURE_WIN_W - 2) // X坐标: 贴近右边缘
#define MEASURE_WIN_Y   25                          // Y坐标

// --- 颜色定义 (RGB565) ---
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COLOR_BG        RGB565(0, 0, 0)       
#define COLOR_GRID      RGB565(60, 60, 60)    
#define COLOR_AXIS      RGB565(120, 120, 120) 
#define COLOR_WAVE      RGB565(0, 255, 0)     
#define COLOR_TEXT      RGB565(255, 255, 255) 
#define COLOR_BAR_BG    RGB565(30, 30, 30)    
#define COLOR_STATUS_OK RGB565(0, 255, 0)     
#define COLOR_STATUS_NO RGB565(255, 0, 0)  
#define COLOR_CURSOR    RGB565(255, 255, 0)   // 黄色测量线
#define COLOR_CURSOR_SEL RGB565(255, 0, 0)    // 选中光标颜色(红)
#define COLOR_OVERLAY   RGB565(20, 20, 40)    // 测量数据背景色
#define COLOR_ALERT_BG  RGB565(50, 0, 0)      // 退出警告框背景
#define COLOR_ALERT_BORDER RGB565(255, 0, 0)  // 退出警告框边框

// --- 配置变量 ---
Uint8 MEASURE_BG_ALPHA = 160; 

// --- 状态定义 ---
typedef struct {
    int paused;             // 暂停状态
    int show_measure;       // 是否显示测量界面
    int volt_div_idx;       // 电压档位索引
    int time_div_idx;       // 时基档位索引
    
    // 测量光标位置
    int cursor_x1, cursor_x2;
    int cursor_y1, cursor_y2;
    int active_cursor;      // 0=X1, 1=X2, 2=Y1, 3=Y2

    // 退出逻辑相关状态
    int show_exit_dialog;   // 是否显示退出确认框
    int start_pressed;      // Start键是否被按下
    Uint32 start_press_time;// Start键按下的时间戳
} AppState;

// --- 物理量定义 ---
float VOLT_PER_DIV[] = {0.5f, 1.0f, 2.0f, 5.0f}; 
const char* VOLT_DIV_STRS[] = {"0.5V", "1.0V", "2.0V", "5.0V"};
float VOLT_SCALES[] = {2.0f, 1.0f, 0.5f, 0.2f}; 
const int VOLT_LEVELS = 4;

float TIME_PER_DIV[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f};
const char* TIME_DIV_STRS[] = {"1ms", "2ms", "5ms", "10ms", "20ms"};
float TIME_SCALES[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f}; 
const int TIME_LEVELS = 5;

// --- 全局变量 ---
int data_buffer[SCREEN_WIDTH];
int serial_fd = -1;
AppState state = {
    0, 0, 
    1, 2, 
    CENTER_X - 50, CENTER_X + 50, 
    CENTER_Y - 40, CENTER_Y + 40,
    0,
    0, 0, 0 // 初始化退出状态
};

// --- 字体库 (保持不变) ---
const unsigned char font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78}, {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78}, {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C}, {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C}, {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C}, {0x44, 0x64, 0x54, 0x4C, 0x44}
};

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
        unsigned char line = bitmap[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) put_pixel(screen, x + col, y + row, color);
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

int open_serial() {
    int fd = open(SERIAL_PORT, O_RDONLY | O_NOCTTY | O_NDELAY);
    if (fd == -1) return -1;
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tcsetattr(fd, TCSANOW, &options);
    fcntl(fd, F_SETFL, FNDELAY);
    return fd;
}

void generate_fake_data(int offset) {
    float freq = TIME_SCALES[state.time_div_idx];
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) data_buffer[i] = data_buffer[i+1];
    data_buffer[SCREEN_WIDTH - 1] = CENTER_Y + (int)(50.0 * sin(offset * freq));
}

void draw_grid(SDL_Surface* screen) {
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    for (int x = CENTER_X; x < SCREEN_WIDTH; x += GRID_SIZE) draw_dotted_v(screen, x, COLOR_GRID);
    for (int x = CENTER_X; x >= 0; x -= GRID_SIZE) draw_dotted_v(screen, x, COLOR_GRID);
    for (int y = CENTER_Y; y < SCREEN_HEIGHT; y += GRID_SIZE) draw_dotted_h(screen, y, COLOR_GRID);
    for (int y = CENTER_Y; y >= 0; y -= GRID_SIZE) draw_dotted_h(screen, y, COLOR_GRID);
    for (int x = 0; x < SCREEN_WIDTH; x++) put_pixel(screen, x, CENTER_Y, COLOR_AXIS);
    for (int y = 0; y < SCREEN_HEIGHT; y++) put_pixel(screen, CENTER_X, y, COLOR_AXIS);
    for (int x = CENTER_X; x < SCREEN_WIDTH; x += GRID_SIZE) {
        for(int h=-2; h<=2; h++) put_pixel(screen, x, CENTER_Y+h, COLOR_AXIS);
        for(int k=1; k<4; k++) {
            int sub = x + (k * GRID_SIZE / 4);
            if (sub < SCREEN_WIDTH) for(int h=-1; h<=1; h++) put_pixel(screen, sub, CENTER_Y+h, COLOR_AXIS);
        }
    }
    for (int x = CENTER_X; x > 0; x -= GRID_SIZE) {
        for(int h=-2; h<=2; h++) put_pixel(screen, x, CENTER_Y+h, COLOR_AXIS);
        for(int k=1; k<4; k++) {
            int sub = x - (k * GRID_SIZE / 4);
            if (sub > 0) for(int h=-1; h<=1; h++) put_pixel(screen, sub, CENTER_Y+h, COLOR_AXIS);
        }
    }
    for (int y = CENTER_Y; y < SCREEN_HEIGHT; y += GRID_SIZE) {
        for(int w=-2; w<=2; w++) put_pixel(screen, CENTER_X+w, y, COLOR_AXIS);
        for(int k=1; k<4; k++) {
            int sub = y + (k * GRID_SIZE / 4);
            if (sub < SCREEN_HEIGHT) for(int w=-1; w<=1; w++) put_pixel(screen, CENTER_X+w, sub, COLOR_AXIS);
        }
    }
    for (int y = CENTER_Y; y > 0; y -= GRID_SIZE) {
        for(int w=-2; w<=2; w++) put_pixel(screen, CENTER_X+w, y, COLOR_AXIS);
        for(int k=1; k<4; k++) {
            int sub = y - (k * GRID_SIZE / 4);
            if (sub > 0) for(int w=-1; w<=1; w++) put_pixel(screen, CENTER_X+w, sub, COLOR_AXIS);
        }
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
}

// --- 计算物理量辅助函数 ---
float pixel_to_time(int x) {
    float time_per_px = TIME_PER_DIV[state.time_div_idx] / (float)GRID_SIZE;
    return (float)(x - CENTER_X) * time_per_px;
}
float pixel_to_volt(int y) {
    float volt_per_px = VOLT_PER_DIV[state.volt_div_idx] / (float)GRID_SIZE;
    return (float)(CENTER_Y - y) * volt_per_px;
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

    SDL_Rect rect = {MEASURE_WIN_X, MEASURE_WIN_Y, MEASURE_WIN_W, MEASURE_WIN_H};
    SDL_Surface* temp = SDL_CreateRGBSurface(SDL_SWSURFACE, rect.w, rect.h, 16, 0, 0, 0, 0);
    if (temp) { SDL_FillRect(temp, NULL, COLOR_OVERLAY); SDL_SetAlpha(temp, SDL_SRCALPHA, MEASURE_BG_ALPHA); SDL_BlitSurface(temp, NULL, screen, &rect); SDL_FreeSurface(temp); }
    else SDL_FillRect(screen, &rect, COLOR_OVERLAY);

    int tx = rect.x + 5, ty = rect.y + 5;
    
    float t1 = pixel_to_time(state.cursor_x1);
    float t2 = pixel_to_time(state.cursor_x2);
    if (fabs(t1) < 1.0f) draw_text_f(screen, tx, ty, (state.active_cursor==0)?COLOR_CURSOR_SEL:COLOR_TEXT, "X1: %.0fus", t1 * 1000.0f);
    else draw_text_f(screen, tx, ty, (state.active_cursor==0)?COLOR_CURSOR_SEL:COLOR_TEXT, "X1: %.2fms", t1);
    if (fabs(t2) < 1.0f) draw_text_f(screen, tx, ty+12, (state.active_cursor==1)?COLOR_CURSOR_SEL:COLOR_TEXT, "X2: %.0fus", t2 * 1000.0f);
    else draw_text_f(screen, tx, ty+12, (state.active_cursor==1)?COLOR_CURSOR_SEL:COLOR_TEXT, "X2: %.2fms", t2);
    float dt = t1 - t2;
    if (fabs(dt) < 1.0f) draw_text_f(screen, tx, ty+24, COLOR_CURSOR, "dt: %.0fus", dt * 1000.0f);
    else draw_text_f(screen, tx, ty+24, COLOR_CURSOR, "dt: %.2fms", dt);

    ty += 38;
    float v1 = pixel_to_volt(state.cursor_y1);
    float v2 = pixel_to_volt(state.cursor_y2);
    if (fabs(v1) < 1.0f) draw_text_f(screen, tx, ty, (state.active_cursor==2)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y1: %.0fmV", v1 * 1000.0f);
    else draw_text_f(screen, tx, ty, (state.active_cursor==2)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y1: %.2fV", v1);
    if (fabs(v2) < 1.0f) draw_text_f(screen, tx, ty+12, (state.active_cursor==3)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y2: %.0fmV", v2 * 1000.0f);
    else draw_text_f(screen, tx, ty+12, (state.active_cursor==3)?COLOR_CURSOR_SEL:COLOR_TEXT, "Y2: %.2fV", v2);
    float dv = v1 - v2;
    if (fabs(dv) < 1.0f) draw_text_f(screen, tx, ty+24, COLOR_CURSOR, "dV: %.0fmV", dv * 1000.0f);
    else draw_text_f(screen, tx, ty+24, COLOR_CURSOR, "dV: %.2fV", dv);
}

// --- 绘制退出确认对话框 ---
void draw_exit_dialog(SDL_Surface* screen) {
    if (!state.show_exit_dialog) return;

    SDL_Rect rect = {CENTER_X - 80, CENTER_Y - 30, 160, 60};
    SDL_FillRect(screen, &rect, COLOR_ALERT_BG);
    
    SDL_Rect border = rect;
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    for(int x = border.x; x < border.x + border.w; x++) {
        put_pixel(screen, x, border.y, COLOR_ALERT_BORDER);
        put_pixel(screen, x, border.y + border.h - 1, COLOR_ALERT_BORDER);
    }
    for(int y = border.y; y < border.y + border.h; y++) {
        put_pixel(screen, border.x, y, COLOR_ALERT_BORDER);
        put_pixel(screen, border.x + border.w - 1, y, COLOR_ALERT_BORDER);
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

    draw_string(screen, rect.x + 40, rect.y + 15, "Confirm Exit?", COLOR_TEXT);
    draw_string(screen, rect.x + 35, rect.y + 35, "Press A to quit", COLOR_TEXT);
}

void draw_ui(SDL_Surface* screen, int connected) {
    SDL_FillRect(screen, NULL, COLOR_BG);
    draw_grid(screen);
    
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    float scale = VOLT_SCALES[state.volt_div_idx];
    for (int x = 0; x < SCREEN_WIDTH - 1; x++) {
        int scaled_y = CENTER_Y + (int)((data_buffer[x] - CENTER_Y) * scale);
        int scaled_next = CENTER_Y + (int)((data_buffer[x+1] - CENTER_Y) * scale);
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
    draw_exit_dialog(screen);

    SDL_Rect bar = {0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20}; SDL_FillRect(screen, &bar, COLOR_BAR_BG);
    SDL_Rect stat = {5, SCREEN_HEIGHT - 14, 8, 8}; SDL_FillRect(screen, &stat, (connected && !state.paused) ? COLOR_STATUS_OK : COLOR_STATUS_NO);
    draw_text_f(screen, 20, SCREEN_HEIGHT - 13, COLOR_TEXT, "Time:%s", TIME_DIV_STRS[state.time_div_idx]);
    draw_text_f(screen, 120, SCREEN_HEIGHT - 13, COLOR_TEXT, "Volt:%s", VOLT_DIV_STRS[state.volt_div_idx]);
    
    if (state.paused) {
        draw_string(screen, 260, SCREEN_HEIGHT - 13, "[PAUSED]", COLOR_STATUS_NO);
    } else if (state.show_measure) {
        draw_string(screen, 260, SCREEN_HEIGHT - 13, "[MEASURE]", COLOR_CURSOR);
    } else {
        draw_string(screen, 280, SCREEN_HEIGHT - 13, "[RUN]", COLOR_STATUS_OK);
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_ShowCursor(SDL_DISABLE); 
    
    SDL_EnableKeyRepeat(300, 30);

    SDL_Surface* screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE);
    
    int running = 1;
    int offset = 0;
    for (int i = 0; i < SCREEN_WIDTH; i++) data_buffer[i] = CENTER_Y;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;

            if (event.type == SDL_KEYDOWN) {
                int key = event.key.keysym.sym;
                
                // --- 1. 处理 Start 键 ---
                if (key == SDLK_RETURN) {
                    if (!state.start_pressed) {
                        state.start_pressed = 1;
                        state.start_press_time = SDL_GetTicks();
                    }
                }

                // --- 2. 处理退出确认逻辑 ---
                if (state.show_exit_dialog) {
                    // 修改点: 互换 A/B 键定义
                    // 原 B 键 (SDLK_LCTRL) 现为取消
                    // 原 A 键 (SDLK_LALT) 现为确认
                    if (key == SDLK_LALT) { // Physical A (Action): 确认退出
                        running = 0;
                    } 
                    else if (key == SDLK_LCTRL || key == SDLK_ESCAPE) { // Physical B (Cancel) / Select: 取消
                        state.show_exit_dialog = 0;
                    }
                    continue; 
                }

                if (key == SDLK_q) running = 0; 
                if (key == SDLK_ESCAPE) state.show_measure = !state.show_measure;
                
                if (key == SDLK_a || key == SDLK_TAB) { if (state.volt_div_idx > 0) state.volt_div_idx--; }
                else if (key == SDLK_s || key == SDLK_BACKSPACE) { if (state.volt_div_idx < VOLT_LEVELS - 1) state.volt_div_idx++; }
                
                int time_changed = 0;
                if (key == SDLK_SPACE) { if (state.time_div_idx > 0) { state.time_div_idx--; time_changed = 1; } }
                else if (key == SDLK_LSHIFT) { if (state.time_div_idx < TIME_LEVELS - 1) { state.time_div_idx++; time_changed = 1; } }
                if (time_changed) for (int i = 0; i < SCREEN_WIDTH; i++) data_buffer[i] = CENTER_Y;

                if (state.show_measure) {
                    // 修改点: 切换光标现在使用 Physical A (SDLK_LALT)
                    if (key == SDLK_LALT) state.active_cursor = (state.active_cursor + 1) % 4;
                    
                    int step = (event.key.keysym.mod & KMOD_SHIFT) ? 10 : 1;
                    
                    switch (key) {
                        case SDLK_LEFT: 
                            if (state.active_cursor == 0) state.cursor_x1 -= step; 
                            if (state.active_cursor == 1) state.cursor_x2 -= step; 
                            break;
                        case SDLK_RIGHT: 
                            if (state.active_cursor == 0) state.cursor_x1 += step; 
                            if (state.active_cursor == 1) state.cursor_x2 += step; 
                            break;
                        case SDLK_UP: 
                            if (state.active_cursor == 2) state.cursor_y1 -= step; 
                            if (state.active_cursor == 3) state.cursor_y2 -= step; 
                            break;
                        case SDLK_DOWN: 
                            if (state.active_cursor == 2) state.cursor_y1 += step; 
                            if (state.active_cursor == 3) state.cursor_y2 += step; 
                            break;
                    }
                } 
            }
            else if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_RETURN) {
                    if (state.start_pressed) {
                        state.start_pressed = 0;
                        if (!state.show_exit_dialog) {
                            state.paused = !state.paused;
                        }
                    }
                }
            }
        }

        if (state.start_pressed && !state.show_exit_dialog) {
            if (SDL_GetTicks() - state.start_press_time > 2000) { 
                state.show_exit_dialog = 1;
            }
        }

        int connected = 0;
        if (serial_fd == -1) serial_fd = open_serial();
        if (!state.paused) {
            if (serial_fd != -1) {
                unsigned char buf[64];
                int n = read(serial_fd, buf, sizeof(buf));
                if (n > 0) {
                    connected = 1;
                    for (int k = 0; k < n; k++) {
                        for (int i = 0; i < SCREEN_WIDTH - 1; i++) data_buffer[i] = data_buffer[i+1];
                        data_buffer[SCREEN_WIDTH - 1] = buf[k]; 
                    }
                }
            }
            if (!connected) { generate_fake_data(offset++); SDL_Delay(10); }
        }
        draw_ui(screen, connected);
        SDL_Flip(screen);
        SDL_Delay(20);
    }
    if (serial_fd != -1) close(serial_fd);
    SDL_Quit();
    return 0;
}
