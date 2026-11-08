#!/usr/bin/env bash
# fetch.sh - every Changeling in apps.txt: cloned if it's missing, pulled if it's here.
cd "$(dirname "$0")"
grep -v '^\s*#' apps.txt | grep . | while read -r app; do
    app=$(echo "$app" | tr -d '\r')
    if [ -d "$app/.git" ]; then
        echo "== $app: pulling"
        git -C "$app" -c http.version=HTTP/1.1 pull --ff-only
    else
        echo "== $app: cloning"
        git -c http.version=HTTP/1.1 clone "https://github.com/CursedPrograms/$app.git" "$app"
    fi
done
