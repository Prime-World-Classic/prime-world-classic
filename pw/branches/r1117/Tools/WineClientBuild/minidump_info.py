#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""minidump_info.py — разбор minidump'а PW_Game.exe без отладчика (stdlib-only).

Зачем: Wine-сборка линкуется без /DEBUG (cl под Wine не пишет PDB — C1902),
cdb/WinDbg в префиксе нет, а собственный хэндлер клиента (Src/System/MiniDump.cpp)
пишет minidump сам. WinDbg на таком дампе печатает «registers are not dumped on
x64» — но это про Exception stream, а не про ThreadList: контексты потоков и
стеки в дампе есть (ThreadList + MemoryList), их просто некому прочитать.

Что печатает:
  * Exception stream: код, адрес, параметры;
  * ThreadList: потоки, наличие CONTEXT (декод x64: RIP/RSP/RBP/RDI/...);
  * ModuleList: базы/размеры модулей (для перевода VA -> модуль+RVA);
  * MemoryList: какие области реально в дампе;
  * --stack: ручная проходка стека (кандидаты-адреса возврата внутри образа)
    с разбором имён по карте линкера (--map, см. map_sym.py о формате строки).

Употребление:
  ./minidump_info.py <dump.mdmp> [--stack] [--map <PW_Game.map>]
                     [--mod <имя модуля>] [--depth N] [--hex N слотов]
"""
import argparse
import bisect
import os
import struct
import sys

STREAM_NAMES = {
    0: "Unused", 1: "MiscInfo", 2: "MemoryDescriptorList", 3: "ThreadList",
    4: "ModuleList", 5: "MemoryList", 6: "Exception", 7: "SystemInfo",
    8: "ThreadNames", 9: "HandleOperationList", 12: "AuxiliaryMemory",
    16: "MemoryInfoList", 17: "ThreadListEx", 18: "HandleData",
    19: "NamedMemoryInfo", 24: "UnhandledException", 25: "MiscInfo3",
}

EXC_CODES = {
    0xC0000005: "ACCESS_VIOLATION", 0xC0000017: "BAD_VM_STATE",
    0xC00000FD: "STACK_OVERFLOW", 0xC0000006: "IN_PAGE_ERROR",
    0xC0000374: "HEAP_CORRUPTION", 0xC0000096: "PRIV_INSTRUCTION",
    0x80000003: "BREAKPOINT", 0xC0000094: "INT_DIVIDE_BY_ZERO",
    0xC0000090: "FLT_INVALID_OPERATION",
}

# CONTEXT x64 (winnt.h): смещения контрольных/целочисленных регистров
CTX_OFF = [
    ("ContextFlags", 0x48), ("MxCsr", 0x4C),
    ("SegCs", 0x50), ("SegDs", 0x52), ("SegEs", 0x54), ("SegFs", 0x56),
    ("SegGs", 0x58), ("SegSs", 0x5A), ("EFlags", 0x5C),
    ("Dr0", 0x60), ("Dr1", 0x68), ("Dr2", 0x70), ("Dr3", 0x78),
    ("Dr6", 0x80), ("Dr7", 0x88),
    ("Rax", 0x90), ("Rcx", 0x98), ("Rdx", 0xA0), ("Rbx", 0xA8),
    ("Rsp", 0xB0), ("Rbp", 0xB8), ("Rsi", 0xC0), ("Rdi", 0xC8),
    ("R8", 0xD0), ("R9", 0xD8), ("R10", 0xE0), ("R11", 0xE8),
    ("R12", 0xF0), ("R13", 0xF8), ("R14", 0x100), ("R15", 0x108),
    ("Rip", 0x110),
]


class Dump(object):
    def __init__(self, path):
        self.d = open(path, "rb").read()
        sig, ver, n, dir_rva = struct.unpack_from("<4sIII", self.d, 0)
        if sig != b"MDMP":
            raise SystemExit("не minidump: %r" % sig)
        self.streams = {}
        for i in range(n):
            st, size, rva = struct.unpack_from("<III", self.d, dir_rva + i * 12)
            if size:
                self.streams.setdefault(st, (size, rva))

    def streams_all(self):
        return self.streams

    # ---- потоки -----------------------------------------------------------
    def threads(self):
        """[(tid, teb, ctx_bytes|None, stack_range|None)]"""
        if 3 not in self.streams:
            return []
        size, rva = self.streams[3]
        cnt = struct.unpack_from("<I", self.d, rva)[0]
        out = []
        off = rva + 4
        # MINIDUMP_THREAD: id, suspend, prio_class, prio, Teb(MEMORY_DESCRIPTOR),
        # ThreadContext(LOCATION_DESCRIPTOR) -> 4*4 + 16 + 8 = 40 байт
        for _ in range(cnt):
            tid, susp, pc, prio = struct.unpack_from("<IIII", self.d, off)
            # Teb(8) Stack.StartOfMemoryRange(8) Stack.Memory{Size,Rva}(8) Ctx{Size,Rva}(8)
            teb = struct.unpack_from("<Q", self.d, off + 16)[0]
            stk_base = struct.unpack_from("<Q", self.d, off + 24)[0]
            stk_size, stk_rva = struct.unpack_from("<II", self.d, off + 32)
            ctx_size, ctx_rva = struct.unpack_from("<II", self.d, off + 40)
            out.append((tid, stk_base, stk_size, stk_rva,
                        self.d[ctx_rva:ctx_rva + ctx_size] if ctx_size else None))
            off += 48
        return out

    def exception(self):
        """(tid, code, flags, addr, params, ctx_bytes)"""
        for st in (24, 6):                      # UnhandledException приоритетнее
            if st not in self.streams:
                continue
            size, rva = self.streams[st]
            tid = struct.unpack_from("<I", self.d, rva)[0]
            # MINIDUMP_EXCEPTION_STREAM: ThreadId(4) + __alignment(4) + ExceptionRecord
            base = rva + 8
            code, flags, rec, addr, nparams = struct.unpack_from("<IIQQI", self.d, base)
            params = struct.unpack_from("<15Q", self.d, base + 32)
            ctx_size, ctx_rva = struct.unpack_from("<II", self.d, base + 32 + 120)
            return (tid, code, flags, addr, list(params[:nparams]),
                    self.d[ctx_rva:ctx_rva + ctx_size] if ctx_size else None)
        return None

    def modules(self):
        """[(base, size, name)]"""
        if 4 not in self.streams:
            return []
        size, rva = self.streams[4]
        cnt = struct.unpack_from("<I", self.d, rva)[0]
        off = rva + 4
        out = []
        for _ in range(cnt):
            base, msize = struct.unpack_from("<QI", self.d, off)
            # MINIDUMP_MODULE: baseOfImage(8) sizeOfImage(4) checksum(4) timestamp(4)
            # ModuleNameRva(4) VersionInfo(52) ... = 108 байт до имени
            name_rva = struct.unpack_from("<I", self.d, off + 20)[0]
            nm = ""
            if name_rva:
                ln = struct.unpack_from("<I", self.d, name_rva)[0]
                nm = self.d[name_rva + 4:name_rva + 4 + ln * 2].decode("utf-16-le", "replace")
            out.append((base, msize, nm))
            off += 108
        return out

    def memory_ranges(self):
        """[(start_va, bytes)] из MemoryList"""
        out = []
        if 5 in self.streams:
            size, rva = self.streams[5]
            cnt = struct.unpack_from("<I", self.d, rva)[0]
            off = rva + 4
            for _ in range(cnt):
                va, msize, mrva = struct.unpack_from("<QII", self.d, off)
                out.append((va, self.d[mrva:mrva + msize]))
                off += 16
        if 12 in self.streams:                  # AuxiliaryMemory (MemoryInfoList)
            size, rva = self.streams[12]
            cnt = struct.unpack_from("<I", self.d, rva)[0]
            off = rva + 4
            for _ in range(cnt):
                aux_rva, aux_size = struct.unpack_from("<II", self.d, off)
                base, msize = struct.unpack_from("<QI", self.d, aux_rva)
                out.append((base, self.d[aux_rva + 12:aux_rva + 12 + msize]))
                off += 8
        out.sort()
        return out

    def read(self, va, n):
        for start, blob in self.memory_ranges():
            if start <= va < start + len(blob):
                return blob[va - start:va - start + n]
        return None


def load_map(path):
    """PW_Game.map -> [(rva, имя)]. В карте адрес дан как "Rva+Base" (x64 — 16
    разрядов, x86 — 8), база берётся из «Preferred load address». Формат строки
    см. map_sym.py."""
    import re
    RE = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8,16})\s+(\S+)\s+"
                    r"([0-9a-fA-F]{8,16})\s+(.*)$")
    base = 0x140000000
    syms = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not syms and line.startswith(" Preferred load address is"):
                base = int(line.split()[-1], 16)
            m = RE.match(line)
            if m:
                rva = int(m.group(4), 16) - base
                if rva >= 0:
                    syms.append((rva, m.group(3)))
    syms.sort()
    return syms


def sym_for(syms, rva):
    if not syms:
        return ""
    vas = [s[0] for s in syms]
    i = bisect.bisect_right(vas, rva) - 1
    if i < 0:
        return ""
    return "%s (+0x%X)" % (syms[i][1], rva - syms[i][0])


def decode_ctx(ctx, exc_addr):
    """Регистры CONTEXT x64. Смещения из winnt.h подтверждаем по Rip==exc_addr;
    если не сошлось — ищем Rip перебором и сдвигаем всю таблицу."""
    if not ctx:
        return None, None
    delta = 0
    rip = struct.unpack_from("<Q", ctx, 0x110)[0] if len(ctx) > 0x118 else None
    if exc_addr and rip != exc_addr:
        for off in range(0, len(ctx) - 8, 8):
            if struct.unpack_from("<Q", ctx, off)[0] == exc_addr:
                delta = off - 0x110
                break
    regs = {}
    for name, off in CTX_OFF:
        o = off + delta
        if 0 <= o < len(ctx) - 7:
            regs[name] = struct.unpack_from("<Q", ctx, o)[0]
    return regs, delta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--map", default=None, help="PW_Game.map (для имён в стеке)")
    ap.add_argument("--mod", default="PW_Game.exe", help="модуль для стека")
    ap.add_argument("--stack", action="store_true", help="проходка стека")
    ap.add_argument("--depth", type=int, default=40)
    ap.add_argument("--hex", type=int, default=0, help="N слотов по RSP/RBP как u64")
    args = ap.parse_args()

    dm = Dump(args.dump)
    print("== %s ==" % args.dump)
    print("потоки:", ", ".join("%s(%d)" % (STREAM_NAMES.get(k, k), v[0])
                               for k, v in sorted(dm.streams.items())))

    mods = dm.modules()
    mod = next(((b, s, n) for b, s, n in mods if n.lower().endswith(args.mod.lower())), None)
    if mod:
        print("модуль %s: base=0x%X size=0x%X" % (mod[2], mod[0], mod[1]))
    print("модулей: %d" % len(mods))

    exc = dm.exception()
    regs = None
    if exc:
        tid, code, flags, addr, params, ctx = exc
        print("\n== Exception ==")
        print("tid=%d code=%#x (%s) flags=%#x addr=%#018x"
              % (tid, code, EXC_CODES.get(code, "?"), flags, addr))
        print("params:", ["%#x" % p for p in params])
        regs, delta = decode_ctx(ctx, addr)
        if regs:
            print("CONTEXT из Exception stream%s:" % ("" if not delta else
                  " (сдвиг таблицы %#X — Rip не на 0x110)" % delta))
            for name in ("Rip", "Rsp", "Rbp", "Rax", "Rbx", "Rcx", "Rdx", "Rsi",
                         "Rdi", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"):
                if name in regs:
                    print("   %-4s = %#018x" % (name, regs[name]))
        else:
            print("CONTEXT в Exception stream нет")

    th = dm.threads()
    print("\n== Потоки (%d) ==" % len(th))
    for tid, sb, ss, srva, ctx in th:
        r2, d2 = decode_ctx(ctx, exc[3] if exc else None)
        mark = ""
        if exc and tid == exc[0]:
            mark = "   <== поток исключения"
        print("  tid=%-7d stack=%#x..%#x (%d Б)  ctx=%s%s"
              % (tid, sb, sb + ss, ss, "есть" if ctx else "НЕТ", mark))
        if exc and tid == exc[0] and r2:
            regs = r2

    if regs and args.hex:
        for base_name in ("Rsp", "Rbp"):
            b = regs.get(base_name)
            if not b:
                continue
            blob = dm.read(b, 8 * args.hex)
            print("\n-- память по %s=%#x (%d слотов) --" % (base_name, b, args.hex))
            for i in range(0, len(blob), 8):
                v = struct.unpack_from("<Q", blob, i)[0]
                print("   +%#04x %#018x" % (i, v))

    if args.stack and mod and regs:
        syms = load_map(args.map) if args.map else []
        base, msize, _ = mod
        print("\n== Проходка стека (%s) ==" % mod[2])
        sp = regs.get("Rsp", 0)
        blob = dm.read(sp, 8 * 512)
        if not blob:
            print("   память стека в дампе не найдена (Rsp=%#x)" % sp)
            return
        seen = 0
        for i in range(0, len(blob), 8):
            v = struct.unpack_from("<Q", blob, i)[0]
            if base + 0x1000 <= v < base + msize:
                rva = v - base
                nm = sym_for(syms, rva)
                print("   RSP+%#05x  %#x  RVA %#x  %s" % (i, v, rva, nm))
                seen += 1
                if seen >= args.depth:
                    break
        if not seen:
            print("   адресов возврата в образе не найдено")


if __name__ == "__main__":
    main()
