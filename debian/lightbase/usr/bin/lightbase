#!/usr/bin/env bash
# ============================================================================
# ⚡ LightBase Studio — Launcher Script
# ============================================================================
# Installed to /usr/local/bin/lightbase by `make install`
# Starts the Python native webview bridge
# ============================================================================

set -e

LIGHTBASE_HOME="${LIGHTBASE_HOME:-/opt/lightbase}"
cd "${LIGHTBASE_HOME}/bridge" && exec python3 python_bridge.py "$@"
