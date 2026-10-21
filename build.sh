#!/bin/bash
meson compile -C build
cp build/engine.out .
cp build/compile_commands.json .
