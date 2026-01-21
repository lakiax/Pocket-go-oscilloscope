#!/bin/sh
LOG=/mnt/apps/scope/log.txt
cd /mnt/apps/scope
echo "--- Starting ---" > $LOG
killall gmenu2x >> $LOG 2>&1
sleep 1
./scope_app >> $LOG 2>&1
echo "Exit code: $?" >> $LOG
cd /mnt/gmenu2x
./gmenu2x &
