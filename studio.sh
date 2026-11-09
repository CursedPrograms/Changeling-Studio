#!/usr/bin/env bash
# Changeling Studio: 1. get every Changeling (git), 2. their requirements, 3. their models (each downloaded once and
# shared), then the Studio window. ./studio.sh window opens only the window.
cd "$(dirname "$0")"
exec python3 studio.py "$@"
