#!/bin/sh
LOG=/mnt/apps/scope/log.txt
cd /mnt/apps/scope

echo "--- Starting Scope App ---" > $LOG

# 1. 切换 Host 模式 (保留)
echo "Switching to HOST mode..." >> $LOG
echo host > /sys/devices/platform/soc/1c13000.usb/musb-hdrc.1.auto/mode 2>> $LOG
sleep 2

# 2. 加载驱动 (既然现在是自动识别/内置的，这行可以注释掉，防止报错)
# 如果你是编译进 zImage 的，完全不需要这行
 insmod /mnt/apps/scope/cdc-acm.ko >> $LOG 2>&1

# 3. 确保权限 (给 ttyACM0 读写权限，防止 APP 没权限打开)
# 虽然 main.c 里有自动修复，但在这里加一道保险更好
if [ -e /dev/ttyACM0 ]; then
    chmod 666 /dev/ttyACM0
fi

# 4. 杀掉主界面进程 (释放屏幕)
killall gmenu2x >> $LOG 2>&1
sleep 1

# 5. 运行示波器 APP
echo "Launching app..." >> $LOG
./scope_app >> $LOG 2>&1

echo "App exited with code: $?" >> $LOG

# 6. 退出后恢复 (可选)
# echo otg > /sys/devices/platform/soc/1c13000.usb/musb-hdrc.1.auto/mode
cd /mnt/gmenu2x
./gmenu2x &