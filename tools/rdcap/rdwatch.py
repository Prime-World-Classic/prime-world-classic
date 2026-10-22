#!/usr/bin/env python3
"""rdwatch.py — наблюдатель за маркером клиента и арм захвата RenderDoc.

Клиент (pw/branches/r1117/Src/Render/nvrhibackend.cpp, env PW_VK_RDCAP*) на
нужной секунде сессии пишет маркер «PWRDCAP 1 seq=.. cur=.. frame=.. sec=..»,
где frame — ЦЕЛЕВОЙ кадр present'а (он в будущем: cur + PW_VK_RDCAP_LEAD).
Здесь читаем маркер и шлём в target-control сервер слоя QueueCapture(frame) —
слой снимает ровно этот кадр (замер: queue 1899 -> «Finished capture, Frame
1899», pw_frame1899.rdc).

Счётчик кадров слоя совпадает со счётчиком present'ов бэкенда, поэтому цель
вычисляется клиентом, а не наблюдателем: наблюдателю не нужно угадывать время.

Использование:
  python3 tools/rd/rdwatch.py --marker ~/PW/wine-client/Game/Bin/rdcap.marker
  python3 tools/rd/rdwatch.py --queue 1899          # разово, без маркера
Порты target-control ищутся по `ss -ltnp` (см. rdctl.ports): под Wine их два —
explorer.exe и PW_Game.exe, берётся процесс клиента.
"""
import argparse
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rdctl  # noqa: E402


def parse_marker(path):
    """'PWRDCAP 1 seq=3 cur=1499 frame=1899 sec=90 api=0' -> dict, иначе None."""
    try:
        with open(path, "r") as f:
            line = f.readline()
    except OSError:
        return None
    if not line.startswith("PWRDCAP"):
        return None
    out = {}
    for part in line.split()[2:]:
        if "=" in part:
            k, v = part.split("=", 1)
            try:
                out[k] = int(v)
            except ValueError:
                out[k] = v
    return out


def client_port():
    """Только порт процесса клиента.

    Под Wine слой грузят ДВА процесса: 38920 = explorer.exe (у него тоже есть
    VkInstance), 38921 = PW_Game.exe. Арм в explorer капчу клиента не даёт
    (замер: queue ушёл в 38920, .rdc нет) — брать строго PW_Game/nvrhi_probe.
    """
    for port, pid, comm in rdctl.listen_ports():
        c = comm.lower()
        if "pw_game" in c or "nvrhi_probe" in c:
            return port
    return 0


def arm(port, frame, listen=25.0):
    """QueueCapture(frame) и ждём NewCapture именно этого кадра.

    Сервер новым подключением переигрывает ВСЕ прежние NewCapture — берём только
    совпадение по кадру, иначе первая же строка будет ложным успехом.
    """
    port, sock = rdctl.connect(port)
    rdctl.read_chunks(sock, 0.5)                       # ответ handshake + переигровка
    sock.sendall(rdctl.pkt(9, struct.pack("<II", frame, 1)))   # ePacket_QueueCapture
    seen = 0
    deadline = time.time() + listen
    while time.time() < deadline:
        for name, info in rdctl.read_chunks(sock, 2.0):
            if name == "NewCapture":
                seen += 1
                m = re.search(r"frame=(\d+)", info)
                if m and int(m.group(1)) == frame:
                    print("  capture: %s" % info)
                    sock.close()
                    return True
    print("  нет NewCapture кадра %d за %.0f с (капч в потоке: %d) — кадр уже прошёл?"
          % (frame, listen, seen))
    sock.close()
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--marker", default=os.path.expanduser("~/PW/wine-client/Game/Bin/rdcap.marker"))
    ap.add_argument("--port", type=int, default=0, help="target-control (0 = найти по ss)")
    ap.add_argument("--queue", type=int, default=0, help="разово: арм на кадр, без маркера")
    ap.add_argument("--extra", type=int, default=0, help="добавить кадров к цели маркера")
    ap.add_argument("--ttl", type=float, default=0.0, help="сколько секунд следить (0 = до Ctrl-C)")
    ap.add_argument("--once", action="store_true", help="выйти после первого маркера")
    args = ap.parse_args()

    port = args.port
    if not port:
        # порт появляется только после vkCreateInstance в процессе клиента
        # (10-20 с от старта) — ищем его в том же цикле, что и маркер
        print("[watch] marker=%s port=ищем PW_Game" % args.marker)
    else:
        print("[watch] marker=%s port=%d" % (args.marker, port))
    if args.queue:
        port = port or client_port()
        if not port:
            print("нет слушателя PW_Game на 38920..38927: клиент запущен без RenderDoc-слоя?")
            return 2
        return 0 if arm(port, args.queue) else 1
    seen = set()
    t0 = time.time()
    while True:
        if not port:
            port = client_port()
            if port:
                print("[watch] port=%d" % port)
        m = parse_marker(args.marker)
        if port and m and m.get("seq") not in seen:
            seen.add(m["seq"])
            target = int(m.get("frame", 0)) + args.extra
            print("[watch] seq=%s sec=%s cur=%s -> queue %d"
                  % (m.get("seq"), m.get("sec"), m.get("cur"), target))
            arm(port, target)
            if args.once:
                return 0
        if args.ttl and time.time() - t0 > args.ttl:
            return 0
        time.sleep(0.2)


if __name__ == "__main__":
    sys.exit(main())
