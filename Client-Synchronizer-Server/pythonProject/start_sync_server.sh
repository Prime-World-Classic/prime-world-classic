#!/bin/bash
cd "$(dirname "$0")"

# Проверяем наличие виртуального окружения
if [ ! -d "venv" ]; then
    echo "Создание virtualenv..."
    python3 -m venv venv
    ./venv/bin/pip install flask waitress requests transliterate
fi

# Проверяем api_key.txt
if [ ! -f "api_key.txt" ]; then
    echo "Ошибка: файл api_key.txt не найден. Создайте его в этой директории."
    exit 1
fi

# Проверяем sync_logs
mkdir -p sync_logs

# Запуск сервера
exec ./venv/bin/python sync_server.py
