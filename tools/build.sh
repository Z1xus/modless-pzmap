#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$(dirname "$0")")" && pwd)
cd "$ROOT"
JAVAC=javac
JAR=jar
if ! command -v "$JAVAC" >/dev/null 2>&1; then echo "need a JDK 25 on PATH"; exit 1; fi
if ! command -v cosmoc++ >/dev/null 2>&1; then echo "need cosmoc++ on PATH"; exit 1; fi
rm -rf build dist
mkdir -p build/agent dist
"$JAVAC" --release 25 -nowarn agent/PZLiveAgent.java -d build/agent
printf 'Premain-Class: PZLiveAgent\nAgent-Class: PZLiveAgent\nCan-Redefine-Classes: false\nCan-Retransform-Classes: false\n' > build/agent/MANIFEST.MF
(cd build/agent && "$JAR" cfm ../../dist/pzm-agent.jar MANIFEST.MF PZLiveAgent.class)
cosmoc++ -std=c++23 -Os -o dist/pzm_live.exe app/src/main.cpp
(cd dist && zip -q -0 pzm_live.exe pzm-agent.jar)
rm -f dist/pzm-agent.jar dist/pzm_live.exe.aarch64.elf dist/pzm_live.exe.com.dbg
ls -la dist
