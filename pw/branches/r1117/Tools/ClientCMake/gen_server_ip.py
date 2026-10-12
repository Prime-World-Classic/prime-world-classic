#!/usr/bin/env python3
"""
gen_server_ip.py — генерация Src/PW_Game/server_ip.h из шаблона.

Двойник sed-блока из build_client_x64_wine.sh: нативной Windows-сборке sed не
доступен, а значения обязаны совпадать (иначе две сборки собирают разный
бинарник). Значения берутся из окружения (PW_SERVER_IP, PW_SESSION_TOKEN,
PW_API_KEY, PW_BACKEND_HTTP_PORT, PW_SERVER_PORT, PW_LOGIN_PORT,
PW_CLUSTER_PORT_FRONT, PW_CLUSTER_PORT_BACK, PW_SYNCHRONIZER_PORT) с теми же
дефолтами, что у Wine-скрипта.

Использование: python3 gen_server_ip.py <template> <out>
"""
import os
import sys

DEFAULTS = {
    "SERVER_IP": "127.0.0.1",
    "SESSION_TOKEN": "Tester00Tester00Tester00Tester00",
    "API_KEY": "changeme",
    "BACKEND_HTTP_PORT": "7777",
    "SERVER_PORT": "27300",
    "LOGIN_PORT": "27301",
    "CLUSTER_PORT_FRONT": "27310",
    "CLUSTER_PORT_BACK": "27340",
    "SYNCHRONIZER_PORT": "27302",
}


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()
    for k in DEFAULTS:
        v = os.environ.get("PW_" + k, DEFAULTS[k])
        text = text.replace("@" + k + "@", v)
    if "@" in text:
        left = [l for l in text.splitlines() if l.strip().startswith("#") and "@" in l]
        print("!! в результате остались подстановки: " + "; ".join(left[:5]),
              file=sys.stderr)
    with open(dst, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print(f"   {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
