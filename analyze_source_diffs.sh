#!/bin/bash
# Обёртка: логика анализа вынесена в analyze_source_diffs.py (python3)
exec python3 "$(dirname "$(readlink -f "$0")")/analyze_source_diffs.py" "$@"
