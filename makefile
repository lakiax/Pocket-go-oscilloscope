# ==========================================
# Pocket Scope 项目构建文件
# ==========================================

# --- 项目名称 ---
TARGET = scope_app

# --- 源文件列表 ---
# 包含主程序、串口驱动(已集成激活逻辑)和数据解析器
SRC = main.c serial_hal.c cursor_pusher.c

# ==========================================
# 编译环境配置
# ==========================================

# --- 1. PC 端模拟 (Ubuntu 本地) ---
CC_PC = gcc
# 使用 sdl-config 自动获取 SDL 依赖, -lm 用于 math.h, -O2 开启优化
CFLAGS_PC = -O2 -lm $(shell sdl-config --cflags --libs)

# --- 2. 掌机端 (Miyoo/PocketGo ARM Docker) ---
# 必须与 Docker 容器内的交叉编译器名称一致
CC_ARM = arm-miyoo-linux-uclibcgnueabi-gcc
# 掌机特定编译参数: 
# -Os (体积优先优化) 
# -lSDL (链接 SDL 库) 
# -D_GNU_SOURCE=1 (启用 Linux 特定扩展)
# -D_REENTRANT (线程安全)
CFLAGS_ARM = -Os -lSDL -lm -D_GNU_SOURCE=1 -D_REENTRANT

# ==========================================
# 编译目标
# ==========================================

.PHONY: all pc arm clean

# 默认输入 'make' 时执行的目标
all: pc

# --- 编译 PC 版 ---
# 生成文件: scope_app_pc
pc: $(SRC)
	@echo "--------------------------------------"
	@echo "Building PC version..."
	@echo "--------------------------------------"
	$(CC_PC) $(SRC) -o $(TARGET)_pc $(CFLAGS_PC)
	@echo "Success! Run with: sudo ./$(TARGET)_pc"

# --- 编译 掌机 版 ---
# 生成文件: scope_app (无后缀)
arm: $(SRC)
	@echo "--------------------------------------"
	@echo "Building ARM (Miyoo) version..."
	@echo "--------------------------------------"
	$(CC_ARM) $(SRC) -o $(TARGET) $(CFLAGS_ARM)
	@echo "Success! Transfer '$(TARGET)' to your device."

# --- 清理编译产物 ---
clean:
	rm -f $(TARGET) $(TARGET)_pc
	@echo "Cleaned up."
