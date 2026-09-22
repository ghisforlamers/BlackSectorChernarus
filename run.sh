#!/bin/bash
set -x
source "config/server.cfg"

SRV_DIR="$CWD/bin/"
MOD_DIR="$CWD/mods/"
MODS=""
for id in "${!MOD_MAP[@]}"; do
    MODS="$MODS${MOD_MAP[$id]};"
done
SERVER_MODS=""
for id in "${!SERVER_MOD_MAP[@]}"; do
    SERVER_MODS="$SERVER_MODS${SERVER_MOD_MAP[$id]};"
done
cd "$SRV_DIR"
./DayZServer                                             \
   -port=2302                                            \
   -limitFPS=25                                          \
   -cpuCount=4                                           \
   -exThreads=4                                          \
   -enableHT                                             \
   -maxMem=8192                                          \
   -profiles="$CWD/profiles"                             \
   -config="$CWD/config/dayz.cfg"                        \
   -mission="$CWD/mpmissions/dayzOffline.chernarusplus"  \
   -storage="$STORAGE_DIR"                               \
   -mod="$MODS"                                          \
   -serverMod="$SERVER_MODS"
