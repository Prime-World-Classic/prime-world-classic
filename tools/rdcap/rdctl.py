#!/usr/bin/env python3
"""rdctl — триггер захвата RenderDoc по target-control протоколу (без GUI).

Захваченный процесс (в т.ч. wine64, в который хостовый Vulkan-загрузчик
загрузил слой VK_LAYER_RENDERDOC_Capture) сам поднимает сервер target-control
на первом свободном порту 38920..38927 (renderdoc/core/core.cpp:655-680,
renderdoc/common/globalconfig.h:143). Слой грузится в несколько процессов
(под Wine — ещё explorer.exe), поэтому порт выбирают по имени процесса.

Протокол (всё little-endian, чанками):
  чанк = uint32 (chunkID | флаги) ; флаги: ChunkIndexMask=0xffff,
         ChunkCallstack 0x10000, ChunkThreadID 0x20000, ChunkDuration 0x40000,
         ChunkTimestamp 0x80000, Chunk64BitSize 0x100000 (serialiser.h:105-110)
       = uint32 длина чанка (читается всегда, serialiser.cpp:175-180;
         в streaming-режиме пишется 0 — serialiser.cpp:431-444)
       = payload
  каждый чанк выровнен до **64 байт** (ChunkAlignment, serialiser.h:1536;
  Reading::EndChunk делает AlignTo<ChunkAlignment>() — serialiser.cpp:314).
  Без выравнивания сервер молчит: застревает в чтении и не отвечает.
  string = uint32 длина + байты без NUL (serialiser.h:1371-1388), bool = 1 байт.

Первым пакетом ОБЯЗАТЕЛЬНО шлём ePacket_Handshake (2): uint32 version=9,
string name, bool kick — только после него сервер заводит
TargetControlClientThread (target_control.cpp:463-531); если имя уже занято и
kick=0 — ответит ePacket_Busy (3) и закроет соединение (532-547).

Пакееты (target_control.cpp:74-90): Noop=1 Handshake=2 Busy=3 NewCapture=4
APIUse=5 TriggerCapture=6 CopyCapture=7 DeleteCapture=8 QueueCapture=9
NewChild=10 CaptureProgress=11 CycleActiveWindow=12 CapturableWindowCount=13
RequestShow=14.
  TriggerCapture payload uint32 numFrames        → TriggerCapture(n)
  QueueCapture   payload uint32 frameNum,nframes → QueueCapture(frameNum+f)
    (матч по счётчику present'ов слоя: core.cpp:1484-1515 +
     vk_core.cpp:3587 ShouldTriggerCapture(m_FrameCounter); кадр в прошлом —
     отбрасывается, поэтому цель ставить в будущем: frameNum = текущий + N)
  NewCapture     payload idx u32, ts u64, path string, thumb bytebuf(u64 len),
                 driver u32, frameNumber u32, byteSize u64, title string
                 (target_control.cpp:243-265) — номер кадра дублируется в
                 имени файла: <CAPFILE>_frame<N>.rdc (core.cpp:1683-1694).

usage:
  rdctl.py ports
  rdctl.py trigger [numFrames] [port]
  rdctl.py queue <frameNum> [numFrames] [port]
  rdctl.py watch [secs] [port]        — только слушать (NewCapture и т.п.)
port 0/пусто = выбрать сам (PW_Game → nvrhi_probe → максимальный).
"""
import re
import socket
import struct
import subprocess
import sys
import time

PKT = {1: "Noop", 2: "Handshake", 3: "Busy", 4: "NewCapture", 5: "APIUse",
       6: "TriggerCapture", 7: "CopyCapture", 8: "DeleteCapture",
       9: "QueueCapture", 10: "NewChild", 11: "CaptureProgress",
       12: "CycleActiveWindow", 13: "CapturableWindowCount", 14: "RequestShow"}
ALIGN = 64
_BUF = {}    # id(sock) -> недописанный хвост потока + сколько уже разобрано


def listen_ports():
    """[(port, pid, comm)] среди 38920..38927 по `ss -ltnp`."""
    out = []
    try:
        r = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True, timeout=5)
        for line in r.stdout.splitlines():
            # users:(("nvrhi_probe",pid=3175484,fd=16)) — comm ДО pid
            mp = re.search(r':(389\d\d)\b', line)
            if not mp:
                continue
            pid = re.search(r'pid=(\d+)', line)
            comm = re.search(r'\("([^"]+)"', line)
            out.append((int(mp.group(1)), int(pid.group(1)) if pid else 0,
                        comm.group(1) if comm else "?"))
    except Exception:
        pass
    if out:
        return sorted(set(out))
    res = []
    for p in range(38920, 38928):
        sk = socket.socket()
        sk.settimeout(0.4)
        try:
            sk.connect(("127.0.0.1", p))
            res.append((p, 0, "?"))
        except OSError:
            pass
        finally:
            sk.close()
    return res


def pw_port(prefer=("PW_Game", "nvrhi_probe", "wine")):
    ps = listen_ports()
    if not ps:
        return 0
    for name in prefer:
        for port, pid, comm in ps:
            if name.lower() in comm.lower():
                return port
    return max(p[0] for p in ps)


def pad(b):
    return b + b"\x00" * (-len(b) % ALIGN)


def pkt(cid, payload=b""):
    return pad(struct.pack("<II", cid, 0) + payload)


def read_chunks(sock, secs=4.0, maxchunks=200):
    """[(имя, инфо)] из потока чанков. Сервер после handshake шлёт непрерывно
    (Noop-пинг раз в 1 с, APIUse, CaptureProgress) — выходим по дедлайну."""
    sock.settimeout(0.5)
    out = []
    buf = _BUF.get(id(sock), b"")     # поток чанков переживает вызовы: сервер
    end = time.time() + secs          # дробит пакеты по TCP-сегментам
    while time.time() < end and len(out) < maxchunks:
        try:
            d = sock.recv(65536)
        except socket.timeout:
            continue
        except OSError:
            break
        if not d:
            break
        buf += d

    def u32(o):
        return struct.unpack_from("<I", buf, o)[0] if o + 4 <= len(buf) else 0

    def u64(o):
        return struct.unpack_from("<Q", buf, o)[0] if o + 8 <= len(buf) else 0

    def string(o):
        n = u32(o)
        if n > 0xFFFF:            # потеря синхронизации
            return "", o + 4
        return buf[o + 4:o + 4 + n].decode("utf-8", "replace"), o + 4 + n

    i = 0
    while i + 4 <= len(buf) and len(out) < maxchunks:
        start = i
        hdr = u32(i); i += 4
        cid = hdr & 0xFFFF
        flags = hdr & ~0xFFFF
        if flags & 0x00010000:   # ChunkCallstack
            i += 4 + u32(i) * 8
        if flags & 0x00020000:   # ChunkThreadID
            i += 4
        if flags & 0x00040000:   # ChunkDuration
            i += 8
        if flags & 0x00080000:   # ChunkTimestamp
            i += 8
        if flags & 0x00100000:   # Chunk64BitSize
            i += 8
        else:
            i += 4               # длина чанка (0 в streaming-режиме)
        info = ""
        if cid == 2:             # Handshake: version, target, pid
            ver = u32(i); i += 4
            tgt, i = string(i)
            pid = u32(i); i += 4
            info = "ver=%d target=%r pid=%d" % (ver, tgt, pid)
        elif cid == 4:           # NewCapture
            idx = u32(i); i += 4
            i += 8                                  # timestamp u64
            path, i = string(i)
            # bytebuf: count u64, затем AlignTo<64> и сами байты
            # (serialiser.h:427-437) — без выравнивания разбор съезжает
            tn = u64(i); i += 8
            i = (i + ALIGN - 1) // ALIGN * ALIGN
            i += tn
            drv = u32(i); i += 4
            fno = u32(i); i += 4
            sz = u64(i); i += 8
            title, i = string(i)
            m = re.search(r"frame(\d+)", path)
            if m:
                fno = int(m.group(1))               # имя файла надёжнее поля
            info = "frame=%d path=%s" % (fno, path)  # size/поле кадра ненадёжны: имя файла точнее
        out.append((PKT.get(cid, "?%d" % cid), info))
        if i > len(buf):            # чанк пришёл не целиком — перечитаем,
            i = start               # когда дойдут байты (иначе разбор съедет)
            break
        i = (i + ALIGN - 1) // ALIGN * ALIGN        # AlignTo<ChunkAlignment>
        if i == start:
            break
    _BUF[id(sock)] = buf[i:] if i <= len(buf) else b""   # разобранные не пережевывать
    return out


def connect(port, name=b"rdctl"):
    if not port:
        port = pw_port()
    if not port:
        raise SystemExit("нет ни одного target-control порта (38920-38927)")
    s = socket.create_connection(("127.0.0.1", port), 3)
    _BUF[id(s)] = b""                     # id() переиспользуется — сбрасываем
    s.sendall(pkt(2, struct.pack("<I", 9) + struct.pack("<I", len(name)) + name + b"\x00"))
    return port, s


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "ports":
        for port, pid, comm in listen_ports():
            print("port=%d pid=%s comm=%s" % (port, pid, comm))
        return 0

    def argport(idx):
        try:
            return int(sys.argv[idx]) if len(sys.argv) > idx else 0
        except ValueError:
            return 0

    if cmd == "trigger":
        n, port = (int(sys.argv[2]) if len(sys.argv) > 2 else 1), argport(3)
        payload = struct.pack("<I", n)
    elif cmd == "queue":
        frame = int(sys.argv[2])
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 1
        port = argport(4)
        payload = struct.pack("<II", frame, n)
    elif cmd == "watch":
        secs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
        port = argport(3)
        port, s = connect(port, b"rdctl-watch")
        print("port=%d" % port)
        for name, info in read_chunks(s, secs):
            print("  %s %s" % (name, info))
        s.close()
        return 0
    elif cmd == "arm":
        # trigger → узнать текущий кадр → queue(cur+delta) → слушать. Нужен для
        # проверки, что QueueCapture срабатывает ровно на заданном кадре.
        delta = int(sys.argv[2])
        port = argport(3)
        port, s = connect(port)
        got = read_chunks(s, 1.0)
        s.sendall(pkt(6, struct.pack("<I", 1)))
        got = read_chunks(s, 10.0)
        caps = [i.split("path=")[1] for n, i in got if n == "NewCapture"]
        if not caps:
            print("port=%d нет NewCapture — арм не проверен" % port)
            return 1
        cur = int(re.search(r"frame(\d+)", caps[-1]).group(1))
        tgt = cur + delta
        print("port=%d cur=%d queue=%d" % (port, cur, tgt))
        s.sendall(pkt(9, struct.pack("<II", tgt, 1)))
        got = read_chunks(s, max(20.0, delta * 0.08))
        caps = [i.split("path=")[1] for n, i in got if n == "NewCapture"]
        print("результат:", caps or "нет капчи")
        s.close()
        return 0
    else:
        print(__doc__)
        return 2

    port, s = connect(port)
    got = read_chunks(s, 1.0)
    hs = [i for n, i in got if n == "Handshake"]
    s.sendall(pkt(6 if cmd == "trigger" else 9, payload))
    got += read_chunks(s, 8.0)
    print("port=%d sent=%s %s" % (port, cmd, " ".join(hs)))
    for name, info in got:
        if name in ("NewCapture", "Busy", "CaptureProgress"):
            print("  %s %s" % (name, info))
    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
