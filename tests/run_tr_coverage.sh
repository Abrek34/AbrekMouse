#!/bin/bash
# Translation coverage test — every GUI string passed to tr()/trf()/tr*()
# helpers must have a Turkish entry in gui/tr.inl.
# Exit 0 = full coverage; exit 1 = missing translations.
set -e
cd "$(dirname "$0")/.."
SRC=(
  gui/main.cpp
  gui/tr.inl
  gui/devices.inl
  gui/daemon_comm.inl
  gui/graph.inl
  gui/widgets_sync.inl
  gui/profile_mgr.inl
  gui/ui_builder.inl
  gui/hidpp_panel.inl
  gui/mouse_test.inl
)
CXX="${CXX:-g++}"
"$CXX" -std=c++20 -Wall -Wextra -O1 -o /tmp/tr_coverage tests/tr_coverage.cpp
/tmp/tr_coverage "${SRC[@]}"