#ifndef CURSOR_PUSHER_H
#define CURSOR_PUSHER_H

#include <SDL/SDL.h>

// --- 可调参数配置 (宏定义) ---
#define PUSHER_HIDE_DELAY_MS   500   // 停止运动多少毫秒后隐藏小人
#define PUSHER_PIXELS_PER_FRAME 4    // 每推动多少像素切换一帧动画

// --- 新增：位置偏移宏 ---
// 控制小人相对于坐标轴的垂直/水平偏移量，使其看起来像“踩”在轴线上
// 正值向右/下偏移，负值向左/上偏移
#define PUSHER_AXIS_OFFSET_X   0     // 上下移动时，相对于Y轴(Center X)的偏移
#define PUSHER_AXIS_OFFSET_Y   0    // 左右移动时，相对于X轴(Center Y)的偏移 (假设小人高50，中心在25，设为25即脚底对齐轴线)

// --- 光标类型定义 ---
typedef enum {
    CURSOR_TYPE_X, // 垂直线光标 (左右移动)
    CURSOR_TYPE_Y  // 水平线光标 (上下移动)
} CursorType;

// --- 接口函数 ---
int Pusher_Init(void);
void Pusher_Cleanup(void);
void Pusher_OnMove(CursorType type, int current_val, int delta);
void Pusher_Render(SDL_Surface* screen);

#endif