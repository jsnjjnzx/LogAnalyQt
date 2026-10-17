#!/bin/zsh
set -eu
preview_dir="${0:A:h}"
preview_build="$preview_dir/../build/ui-preview"
cmake -S "$preview_dir" -B "$preview_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=/opt/homebrew/opt/qtdeclarative
cmake --build "$preview_build" -j 8
open "$preview_build/LogAnalyQtPreview.app"
