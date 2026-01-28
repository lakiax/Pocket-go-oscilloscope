#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

// 初始化音频系统，加载音效文件
// filename: wav文件名
// 返回: 0 成功, -1 失败
int Audio_Init(const char* filename);

// 播放音效
// 如果正在播放，会重置并重新播放
void Audio_Play(void);

// 清理音频资源
void Audio_Cleanup(void);

#endif
