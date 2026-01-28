#include "serial_hal.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

int serial_open(const char* port_name) {
    // 1. 阻塞模式打开
    int fd = open(port_name, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        return -1;
    }

    // 2. 设置串口参数
    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        close(fd);
        return -1;
    }
    
    cfmakeraw(&options); 
    
    // --- 关键修改：提速到 921600 ---
    // 这需要 ESP32 端同时也设置为 921600
    cfsetispeed(&options, B921600);
    cfsetospeed(&options, B921600);
    
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CRTSCTS; // 禁用硬件流控
    
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        close(fd);
        return -1;
    }

    // 3. 复位序列 (DTR/RTS)
    // 用于重启 ESP32，确保从头开始运行
    int status;
    if (ioctl(fd, TIOCMGET, &status) != -1) {
        // A. 拉低 (Reset)
        status &= ~TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);
        usleep(1000); 
        
        status &= ~TIOCM_RTS;
        ioctl(fd, TIOCMSET, &status);
        usleep(100000); 

        // B. 拉高 (Active)
        status |= TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);
        usleep(1000); 
        
        status |= TIOCM_RTS;
        ioctl(fd, TIOCMSET, &status);
        
        // C. 等待 ESP32 重启
        usleep(500000); 
    }

    // 4. 切换到非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NDELAY);

    tcflush(fd, TCIFLUSH);
    return fd;
}

int serial_read_bytes(int fd, uint8_t* buffer, int max_len) {
    if (fd < 0) return -1;
    return read(fd, buffer, max_len);
}

void serial_close(int fd) {
    if (fd >= 0) close(fd);
}
