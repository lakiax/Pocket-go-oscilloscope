#include "cursor_pusher.h"
#include <stdlib.h> // abs()
#include <string.h> // memcpy

// --- 内部状态 ---
// 四个方向的精灵表
static SDL_Surface* spr_right = NULL; // 原图
static SDL_Surface* spr_left  = NULL; // 镜像
static SDL_Surface* spr_down  = NULL; // 旋转90度 CW
static SDL_Surface* spr_up    = NULL; // 旋转90度 CCW (或镜像后旋转)

static int is_visible = 0;
static Uint32 last_move_time = 0;

// 位置与动画
static int draw_x = 0;
static int draw_y = 0;
static int anim_frame = 0;
static int anim_direction = 1;   // 1: 正序(0->7), -1: 倒序(7->0)
static int dist_accumulator = 0; // 累积移动距离
static int current_dir_type = 0; // 0:R, 1:L, 2:D, 3:U

// 素材参数
#define SPRITE_W 28
#define SPRITE_H 50
#define TOTAL_FRAMES 8

// --- 图像处理辅助函数 ---

// 获取像素值
static Uint32 get_pixel(SDL_Surface *surface, int x, int y) {
    int bpp = surface->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch(bpp) {
        case 1: return *p;
        case 2: return *(Uint16 *)p;
        case 3: if(SDL_BYTEORDER == SDL_BIG_ENDIAN) return p[0] << 16 | p[1] << 8 | p[2];
                else return p[0] | p[1] << 8 | p[2] << 16;
        case 4: return *(Uint32 *)p;
        default: return 0;
    }
}

// 设置像素值
static void put_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    int bpp = surface->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch(bpp) {
        case 1: *p = pixel; break;
        case 2: *(Uint16 *)p = pixel; break;
        case 3: if(SDL_BYTEORDER == SDL_BIG_ENDIAN) { p[0] = (pixel >> 16) & 0xff; p[1] = (pixel >> 8) & 0xff; p[2] = pixel & 0xff; }
                else { p[0] = pixel & 0xff; p[1] = (pixel >> 8) & 0xff; p[2] = (pixel >> 16) & 0xff; } break;
        case 4: *(Uint32 *)p = pixel; break;
    }
}

// 创建水平镜像 (Right -> Left)
static SDL_Surface* create_mirror_surface(SDL_Surface* src, int w, int h, int frames) {
    SDL_Surface* dst = SDL_CreateRGBSurface(SDL_SWSURFACE, src->w, src->h, src->format->BitsPerPixel, 
                                            src->format->Rmask, src->format->Gmask, src->format->Bmask, src->format->Amask);
    if (!dst) return NULL;

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    for (int f = 0; f < frames; f++) {
        int offset_x = f * w;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Uint32 px = get_pixel(src, offset_x + x, y);
                // 镜像：源的左边是目标的右边
                put_pixel(dst, offset_x + (w - 1 - x), y, px);
            }
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    // 设置透明色
    SDL_SetColorKey(dst, SDL_SRCCOLORKEY, src->format->colorkey);
    return dst;
}

// 创建旋转90度后的序列 (Right -> Down/Up)
// clockwise: 1=顺时针(Down), 0=逆时针(Up)
// 注意：旋转后单帧尺寸变为 H x W，总宽度变为 H * frames
static SDL_Surface* create_rotated_surface(SDL_Surface* src, int w, int h, int frames, int clockwise) {
    int new_w = h;
    int new_h = w;
    int total_w = new_w * frames;

    SDL_Surface* dst = SDL_CreateRGBSurface(SDL_SWSURFACE, total_w, new_h, src->format->BitsPerPixel, 
                                            src->format->Rmask, src->format->Gmask, src->format->Bmask, src->format->Amask);
    if (!dst) return NULL;

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    for (int f = 0; f < frames; f++) {
        int src_offset_x = f * w;
        int dst_offset_x = f * new_w;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Uint32 px = get_pixel(src, src_offset_x + x, y);
                int dx, dy;
                
                if (clockwise) { // 顺时针 90度 (Push Down)
                    // (x, y) -> (h-1-y, x)
                    dx = (h - 1 - y);
                    dy = x;
                } else { // 逆时针 90度 (Push Up)
                    // (x, y) -> (y, w-1-x)
                    dx = y;
                    dy = (w - 1 - x);
                }
                
                put_pixel(dst, dst_offset_x + dx, dy, px);
            }
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    SDL_SetColorKey(dst, SDL_SRCCOLORKEY, src->format->colorkey);
    return dst;
}

// --- 主逻辑 ---

int Pusher_Init(void) {
    // 1. 加载原图 (Right)
    spr_right = SDL_LoadBMP("walk.bmp"); 
    if (!spr_right) return -1;
    
    // 设置透明色 (洋红 255,0,255)
    Uint32 colorkey = SDL_MapRGB(spr_right->format, 255, 0, 255);
    SDL_SetColorKey(spr_right, SDL_SRCCOLORKEY, colorkey);

    // 2. 生成镜像 (Left) - 用于从右往左推
    spr_left = create_mirror_surface(spr_right, SPRITE_W, SPRITE_H, TOTAL_FRAMES);

    // 3. 生成旋转 (Down) - 用于从上往下推
    // 顺时针旋转，人物头朝右，脚朝左，适合垂直轴右侧向下推
    spr_down = create_rotated_surface(spr_right, SPRITE_W, SPRITE_H, TOTAL_FRAMES, 1);

    // 4. 生成旋转 (Up) - 用于从下往上推
    // 逆时针旋转，人物头朝左，脚朝右，适合垂直轴右侧向上推
    // 这里我们基于 spr_right (向右推) 逆时针转，变成向上推
    spr_up = create_rotated_surface(spr_right, SPRITE_W, SPRITE_H, TOTAL_FRAMES, 0);

    if (!spr_left || !spr_down || !spr_up) return -1;

    return 0;
}

void Pusher_Cleanup(void) {
    if (spr_right) SDL_FreeSurface(spr_right);
    if (spr_left)  SDL_FreeSurface(spr_left);
    if (spr_down)  SDL_FreeSurface(spr_down);
    if (spr_up)    SDL_FreeSurface(spr_up);
    spr_right = spr_left = spr_down = spr_up = NULL;
}

void Pusher_OnMove(CursorType type, int current_val, int delta) {
    if (delta == 0) return;

    // 1. 激活显示
    is_visible = 1;
    last_move_time = SDL_GetTicks();

    // 2. 计算动画帧 (往复循环: 0->7->0)
    dist_accumulator += abs(delta);
    if (dist_accumulator >= PUSHER_PIXELS_PER_FRAME) {
        anim_frame += anim_direction;
        
        // 边界检查与反向
        if (anim_frame >= TOTAL_FRAMES - 1) {
            anim_frame = TOTAL_FRAMES - 1;
            anim_direction = -1; // 到底了，倒序回退
        } else if (anim_frame <= 0) {
            anim_frame = 0;
            anim_direction = 1;  // 到头了，正序前进
        }
        
        dist_accumulator = 0;
    }

    // 3. 计算显示位置与方向
    // 屏幕中心参考
    int center_x = 320 / 2;
    int center_y = 240 / 2;

    if (type == CURSOR_TYPE_X) {
        // --- 垂直光标 (左右推) ---
        // 小人"踩"在 X 轴上，也就是 Y = center_y + offset
        // 注意：绘图坐标是左上角，所以要减去图片高度
        draw_y = (center_y + PUSHER_AXIS_OFFSET_Y) - SPRITE_H;
        
        if (delta > 0) {
            // 向右移 -> 小人在光标左侧推 -> 使用原图 (Right)
            current_dir_type = 0; 
            draw_x = current_val - SPRITE_W;
        } else {
            // 向左移 -> 小人在光标右侧推 -> 使用镜像 (Left)
            current_dir_type = 1;
            draw_x = current_val; 
        }
    } else {
        // --- 水平光标 (上下推) ---
        // 小人"踩"在 Y 轴上，也就是 X = center_x + offset
        // 旋转后，图片宽变成了原高(50)，高变成了原宽(28)
        // 假设 OFFSET_X 是让脚(旋转后的底部)对齐轴线
        // 注意：旋转后的宽度是 SPRITE_H (50)，高度是 SPRITE_W (28)
        
        if (delta > 0) {
            // 向下移 (Y增加) -> 小人在光标上方推 -> 使用旋转图 (Down)
            current_dir_type = 2;
            // 垂直方向：在光标上方
            draw_y = current_val - SPRITE_W; // 旋转后的高度是 SPRITE_W
            
            // 水平方向：踩在Y轴 (center_x)
            // 假设我们希望旋转后人物的"脚"侧贴着轴。
            // 顺时针旋转后，原底部(脚)在左侧。
            // 所以 draw_x = center_x + offset
            draw_x = center_x + PUSHER_AXIS_OFFSET_X;
        } else {
            // 向上移 (Y减小) -> 小人在光标下方推 -> 使用旋转图 (Up)
            current_dir_type = 3;
            // 垂直方向：在光标下方
            draw_y = current_val; 
            
            // 水平方向：踩在Y轴
            // 逆时针旋转后，原底部(脚)在右侧。
            // 所以 draw_x = center_x + offset - image_width
            draw_x = (center_x + PUSHER_AXIS_OFFSET_X) - SPRITE_H;
        }
    }
}

void Pusher_Render(SDL_Surface* screen) {
    if (!is_visible) return;

    if (SDL_GetTicks() - last_move_time > PUSHER_HIDE_DELAY_MS) {
        is_visible = 0;
        anim_frame = 0;
        anim_direction = 1; // 重置为正向
        return;
    }

    SDL_Surface* curr_spr = NULL;
    int w, h;

    // 选择对应的精灵表和尺寸
    switch(current_dir_type) {
        case 0: curr_spr = spr_right; w = SPRITE_W; h = SPRITE_H; break;
        case 1: curr_spr = spr_left;  w = SPRITE_W; h = SPRITE_H; break;
        case 2: curr_spr = spr_down;  w = SPRITE_H; h = SPRITE_W; break; // 旋转后宽高互换
        case 3: curr_spr = spr_up;    w = SPRITE_H; h = SPRITE_W; break; // 旋转后宽高互换
        default: return;
    }

    if (curr_spr) {
        SDL_Rect src_rect = { anim_frame * w, 0, w, h };
        SDL_Rect dst_rect = { draw_x, draw_y, 0, 0 };
        
        if (draw_x > -w && draw_x < screen->w && 
            draw_y > -h && draw_y < screen->h) {
            SDL_BlitSurface(curr_spr, &src_rect, screen, &dst_rect);
        }
    } else {
        // 后备红方块
        SDL_Rect rect = { draw_x, draw_y, (current_dir_type >= 2) ? SPRITE_H : SPRITE_W, 
                                          (current_dir_type >= 2) ? SPRITE_W : SPRITE_H };
        SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, 255, 0, 0));
    }
}