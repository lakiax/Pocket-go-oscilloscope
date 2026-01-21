# --- 项目配置 ---
TARGET = scope_app
SRC = main.c

# --- 模式 1: PC 模拟 (Ubuntu 本地) ---
CC_PC = gcc
# 自动获取 SDL 库路径和参数
CFLAGS_PC = -O2 -lm $(shell sdl-config --cflags --libs)

# --- 模式 2: PocketGo (ARM Docker) ---
CC_ARM = arm-miyoo-linux-uclibcgnueabi-gcc
CFLAGS_ARM = -Os -lSDL -lm -D_GNU_SOURCE=1 -D_REENTRANT

# --- 编译目标 ---

# 1. 默认输入 'make' 或 'make pc' -> 生成 Ubuntu 可执行文件
pc: $(SRC)
	$(CC_PC) $(SRC) -o $(TARGET)_pc $(CFLAGS_PC)
	@echo "编译完成！运行 ./$(TARGET)_pc 启动模拟器"

# 2. 输入 'make arm' -> 生成掌机可执行文件 (配合 Docker 使用)
arm: $(SRC)
	$(CC_ARM) $(SRC) -o $(TARGET) $(CFLAGS_ARM)

clean:
	rm -f $(TARGET) $(TARGET)_pc
