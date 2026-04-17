#!/usr/bin/env sh

set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <clap-bundle|vst2-fixture|plugin-id>" >&2
    exit 1
fi

case "$1" in
    clap-bundle)
        printf '%s\n' "build/keepsake.clap"
        ;;
    vst2-fixture)
        if [ -d "build/test-plugin.vst" ]; then
            printf '%s\n' "build/test-plugin.vst"
        elif [ -f "build/test-plugin.so" ]; then
            printf '%s\n' "build/test-plugin.so"
        elif [ -f "build/test-plugin.dll" ]; then
            printf '%s\n' "build/test-plugin.dll"
        elif [ -f "build/test-plugin.dylib" ]; then
            printf '%s\n' "build/test-plugin.dylib"
        else
            echo "demo asset not found: test-plugin build output" >&2
            exit 1
        fi
        ;;
    gui-vst2-fixture)
        if [ -d "build/demo-vst2-gui-plugin.vst" ]; then
            printf '%s\n' "build/demo-vst2-gui-plugin.vst"
        elif [ -f "build/demo-vst2-gui-plugin.so" ]; then
            printf '%s\n' "build/demo-vst2-gui-plugin.so"
        elif [ -f "build/demo-vst2-gui-plugin.dll" ]; then
            printf '%s\n' "build/demo-vst2-gui-plugin.dll"
        elif [ -f "build/demo-vst2-gui-plugin.dylib" ]; then
            printf '%s\n' "build/demo-vst2-gui-plugin.dylib"
        else
            echo "demo asset not found: demo-vst2-gui-plugin build output" >&2
            exit 1
        fi
        ;;
    plugin-id)
        printf '%s\n' "keepsake.vst2.4B505354"
        ;;
    gui-plugin-id)
        printf '%s\n' "keepsake.vst2.4B504755"
        ;;
    *)
        echo "unknown asset key: $1" >&2
        exit 1
        ;;
esac
