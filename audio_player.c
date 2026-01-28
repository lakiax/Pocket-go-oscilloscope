#include "audio_player.h"
#include <SDL/SDL.h>
#include <stdio.h>

// 音频资源变量
static Uint8 *audio_chunk = NULL;
static Uint32 audio_len = 0;
static Uint8 *audio_pos = NULL;
static Uint32 audio_len_remaining = 0;

// SDL 音频回调函数：系统需要数据时自动调用
static void audio_callback(void *udata, Uint8 *stream, int len) {
    // 先将缓冲区静音，避免噪音
    SDL_memset(stream, 0, len);

    if (audio_len_remaining == 0) return;

    // 计算实际需要拷贝的长度
    len = (len > audio_len_remaining ? audio_len_remaining : len);

    // 混合音频数据 (SDL_MIX_MAXVOLUME 确保音量正常)
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);

    // 更新指针和剩余长度
    audio_pos += len;
    audio_len_remaining -= len;
}

int Audio_Init(const char* filename) {
    SDL_AudioSpec wanted_spec;

    // 加载 WAV 文件
    if (SDL_LoadWAV(filename, &wanted_spec, &audio_chunk, &audio_len) == NULL) {
        fprintf(stderr, "Audio Load Failed: %s\n", SDL_GetError());
        return -1;
    }

    // 设置回调
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = NULL;

    // 打开音频设备
    if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
        fprintf(stderr, "Audio Open Failed: %s\n", SDL_GetError());
        SDL_FreeWAV(audio_chunk);
        return -1;
    }

    audio_len_remaining = 0;
    audio_pos = audio_chunk;

    // 开始播放（解除暂停状态）
    SDL_PauseAudio(0);
    return 0;
}

void Audio_Play(void) {
    if (!audio_chunk) return;

    // 加锁以安全修改回调函数正在使用的变量
    SDL_LockAudio();
    audio_pos = audio_chunk;
    audio_len_remaining = audio_len;
    SDL_UnlockAudio();
}

void Audio_Cleanup(void) {
    SDL_CloseAudio();
    if (audio_chunk) {
        SDL_FreeWAV(audio_chunk);
        audio_chunk = NULL;
    }
}
