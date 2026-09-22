#!/bin/bash
set -x
source "config/server.cfg"

SRV_DIR="$CWD/bin"
MOD_DIR="$CWD/mods"
steamcmd \
    +force_install_dir "$SRV_DIR" \
    +login "$STEAM_USER"          \
    +app_update 223350 validate   \
    +quit
MODS=""
for id in "${!MOD_MAP[@]}"; do
    MODS="$MODS +workshop_download_item 221100 $id"
done
for id in "${!SERVER_MOD_MAP[@]}"; do
    MODS="$MODS +workshop_download_item 221100 $id"
done
steamcmd                                      \
    +force_install_dir "$MOD_DIR"             \
    +login "$STEAM_USER"                      \
    $MODS                                     \
    +quit
for id in "${!MOD_MAP[@]}"; do
    dst="$SRV_DIR/${MOD_MAP[$id]}"
    src="$MOD_DIR/steamapps/workshop/content/221100/$id"
    ln -sfn $src $dst
done
for id in "${!SERVER_MOD_MAP[@]}"; do
    dst="$SRV_DIR/${SERVER_MOD_MAP[$id]}"
    src="$MOD_DIR/steamapps/workshop/content/221100/$id"
    ln -sfn $src $dst
done
cp -p "$MOD_DIR/steamapps/workshop/content/221100"/*/keys/*.bikey "$SRV_DIR/keys"
cp -p "$MOD_DIR/steamapps/workshop/content/221100"/*/Keys/*.bikey "$SRV_DIR/keys"
cp -p "$MOD_DIR/steamapps/workshop/content/221100"/*/key/*.bikey "$SRV_DIR/keys"
