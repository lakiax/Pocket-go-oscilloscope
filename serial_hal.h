#ifndef SERIAL_HAL_H
#define SERIAL_HAL_H
#include <stdint.h>

int serial_open(const char* port_name);
int serial_read_bytes(int fd, uint8_t* buffer, int max_len);
void serial_close(int fd);

#endif
