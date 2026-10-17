#!/bin/zsh
set -eu
exec "${0:A:h}/run_new_ui.command" --legacy-ui
