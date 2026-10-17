#!/bin/zsh
set -eu
project_dir="${0:A:h:h}"
cd "$project_dir"
if [[ ! -f "$project_dir/build/macos-debug/build.ninja" ]]; then
    cmake --preset macos-debug
fi
cmake --build --preset macos-debug --target appLogAnalyQt bundle_macos_updater
open "$project_dir/build/macos-debug/appLogAnalyQt.app" --args "$@"
