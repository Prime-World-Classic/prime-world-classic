#!/usr/bin/env python3
# shader_budget.py — бюджет дескрипторов по реальному корпусу шейдеров
# (PLAN_NVRHI §3.w, §2.6; вход для макета дескриптор-сетов этапа 3).
#
# Вопрос, на который отвечает: сколько ресурсов (текстур, сэмплеров, uniform-
# буферов, их размеров) требует ОДИН пайплайн клиента, и где uniform-буфер
# индексируется НЕконстантным индексом. Второе — ограничение переносимости:
# в Vulkan неконстантная (non-uniform) индексация члена UBO даёт
# неопределённое поведение, в D3D12 запрещена прямо; такие буферы обязаны
# стать SSBO/структурным буфером.
#
# Как: те же материалы и те же PIN'ы, что у боевого генератора (MaterialSpec.xml),
# та же прослойка SM3->SM6 (spirv_probe/shim.py), тот же DXC, что и в спайке A.
# Только результат компиляции не выбрасывается, а разбирается как SPIR-V.
#
# Запуск:  ../spirv_probe/fetch_dxc.sh   (если DXC ещё не скачан)
#          ./shader_budget.py [--no-shim] [--only NAME] [--pins N] [--dump] [--keep]
# (--keep не удаляет сгенерированное дерево зумов в Src/Render/zzz_budget_shaders_*)
import os, re, sys, atexit, shutil, subprocess, collections, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.abspath(os.path.join(HERE, "..", "spirv_probe"))
sys.path.insert(0, PROBE)
import shim                                             # noqa: E402

R1117 = os.path.abspath(os.path.join(HERE, "..", "..", "pw", "branches", "r1117"))
SH = os.path.join(R1117, "Src", "Render", "Shaders")
RENDER = os.path.join(R1117, "Src", "Render")
DXC = os.path.join(PROBE, "vendor", "dxc", "bin", "dxc")
ENV = dict(os.environ, LD_LIBRARY_PATH=os.path.join(PROBE, "vendor", "dxc", "lib"))

# ---------------------------------------------------------------- SPIR-V read
# Разбор минимальный: только то, что нужно для бюджета. Значения опкодов и
# декораций — по SPIR-V 1.5 spec (DXC выпускает 1.3-1.5).
OP_NAME, OP_MEMBER_NAME = 5, 6
OP_DECORATE, OP_MEMBER_DECORATE, OP_DECORATE_STRING = 71, 72, 73
OP_TYPE_VOID, OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT = 19, 20, 21, 22
OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE, OP_TYPE_SAMPLER = 23, 24, 25, 26
OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY = 27, 28, 29
OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_TYPE_FUNCTION = 30, 32, 33
OP_CONSTANT, OP_CONSTANT_COMPOSITE, OP_UNDEF = 43, 44, 4
OP_VARIABLE, OP_LOAD, OP_STORE = 59, 61, 62
OP_ACCESS_CHAIN, OP_INBOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN = 65, 66, 67
OP_FUNCTION, OP_FUNCTION_PARAMETER = 54, 54
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_ARRAY_STRIDE = 2, 3, 6
DEC_MATRIX_STRIDE, DEC_BUILT_IN, DEC_BINDING = 7, 11, 33
DEC_DESCRIPTOR_SET, DEC_OFFSET = 34, 35
SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_PRIVATE = 0, 2, 7
SC_FUNCTION, SC_STORAGE_BUFFER = 6, 12
ACCESS_CHAINS = (OP_ACCESS_CHAIN, OP_INBOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN)


def words_of(path):
    data = open(path, "rb").read()
    if len(data) < 20 or int.from_bytes(data[:4], "little") != 0x07230203:
        raise SystemExit("не SPIR-V: %s (%d байт)" % (path, len(data)))
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]


class Module:
    """Индексы SPIR-V-модуля, достаточные для бюджета ресурсов."""

    def __init__(self, path):
        w = words_of(path)
        self.w = w
        self.name = {}            # id -> имя (OpName)
        self.member_name = {}     # (id, индекс) -> имя
        self.decorates = collections.defaultdict(list)     # id -> [(dec, args)]
        self.member_decorates = collections.defaultdict(list)
        self.types = {}           # id -> (opcode, args)
        self.variables = []       # (id, name, ptr_type_id, storage_class)
        self.const_ids = set()
        self.accesses = []        # (resultType, result, base, [index ids])
        self.loads = []         # (result, base ptr id)
        i = 5                     # после заголовка
        while i < len(w):
            wc, op = w[i] >> 16, w[i] & 0xFFFF
            ins = w[i:i + wc]
            i += wc
            if op == OP_NAME and wc >= 3:
                self.name[ins[1]] = self._str(ins[2:])
            elif op == OP_MEMBER_NAME and wc >= 4:
                self.member_name[(ins[1], ins[2])] = self._str(ins[3:])
            elif op == OP_DECORATE and wc >= 3:
                self.decorates[ins[1]].append((ins[2], ins[3:]))
            elif op == OP_MEMBER_DECORATE and wc >= 4:
                self.member_decorates[(ins[1], ins[2])].append((ins[3], ins[4:]))
            elif op in (OP_TYPE_VOID, OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT,
                        OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE,
                        OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY,
                        OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER,
                        OP_TYPE_FUNCTION):
                self.types[ins[1]] = (op, ins[2:])
            elif op == OP_VARIABLE and wc >= 4:
                # OpVariable: resultType, resultId, storage class[, init]
                self.variables.append((ins[2], self.name.get(ins[2], "?"),
                                       ins[1], ins[3]))
            elif op in (OP_CONSTANT, OP_CONSTANT_COMPOSITE, OP_UNDEF):
                # word0, resultType, result
                self.const_ids.add(ins[2])
            elif op in ACCESS_CHAINS and wc >= 5:
                # word0, resultType, result, base, indexes...
                self.accesses.append((ins[1], ins[2], ins[3], ins[4:]))
            elif op == OP_LOAD and wc >= 4:
                self.loads.append((ins[2], ins[3]))

    @staticmethod
    def _str(parts):
        out = bytearray()
        for word in parts:
            for k in range(4):
                b = (word >> (8 * k)) & 0xFF
                if b == 0:
                    return out.decode("utf-8", "replace")
                out.append(b)
        return out.decode("utf-8", "replace")

    # --- разбор типов ------------------------------------------------------
    def pointee(self, type_id):
        op, args = self.types.get(type_id, (None, ()))
        if op == OP_TYPE_POINTER:
            return args[1]
        return type_id

    def kind_of(self, type_id, depth=0):
        """'texture' | 'sampler' | 'ubo' | 'ssbo' | 'other' + доп. сведения."""
        if depth > 8:
            return "other", None
        op, args = self.types.get(type_id, (None, ()))
        if op in (OP_TYPE_IMAGE, OP_TYPE_SAMPLED_IMAGE):
            return "texture", args
        if op == OP_TYPE_SAMPLER:
            return "sampler", args
        if op == OP_TYPE_STRUCT:
            decs = [d for d, _ in self.decorates.get(type_id, [])]
            if DEC_BUFFER_BLOCK in decs:
                return "ssbo", args
            if DEC_BLOCK in decs:
                return "ubo", args
            return "struct", args
        if op == OP_TYPE_RUNTIME_ARRAY:
            return "ssbo", args
        if op == OP_TYPE_ARRAY:
            return self.kind_of(args[0], depth + 1)
        return "other", args

    def size_of(self, type_id, depth=0):
        """Размер в байтах (для uniform-буферов — с учётом layout-декораций)."""
        if depth > 12:
            return 0
        op, args = self.types.get(type_id, (None, ()))
        if op in (OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT):
            return 4
        if op == OP_TYPE_VECTOR:
            return args[1] * self.size_of(args[0], depth + 1)
        if op == OP_TYPE_MATRIX:
            stride = 0
            for d, a in self.decorates.get(type_id, []):
                if d == DEC_MATRIX_STRIDE:
                    stride = a[0]
            return args[1] * (stride or self.size_of(args[0], depth + 1))
        if op == OP_TYPE_ARRAY:
            n = 0
            nid = args[1]
            if nid in self.types and self.types[nid][0] == OP_TYPE_INT:
                # длина массива — OpConstant, его значение в модуле не храним:
                # берём из декорации ArrayStride * count, count ищется в OpConstant
                n = self.const_value(nid)
            stride = 0
            for d, a in self.decorates.get(type_id, []):
                if d == DEC_ARRAY_STRIDE:
                    stride = a[0]
            if not stride:
                stride = self.size_of(args[0], depth + 1)
            return n * stride
        if op == OP_TYPE_STRUCT:
            total = 0
            for idx, mid in enumerate(args):
                off = None
                for d, a in self.member_decorates.get((type_id, idx), []):
                    if d == DEC_OFFSET:
                        off = a[0]
                if off is None:
                    off = total
                total = max(total, off + self.size_of(mid, depth + 1))
            return total
        if op == OP_TYPE_RUNTIME_ARRAY:
            return 0
        return 0

    def const_value(self, id_):
        """Целое из OpConstant (для длин массивов).
        OpConstant: word0, resultType, resultId, value..."""
        w = self.w
        i = 5
        while i < len(w):
            wc, op = w[i] >> 16, w[i] & 0xFFFF
            if op == OP_CONSTANT and wc >= 4 and w[i + 2] == id_:
                return w[i + 3]
            i += wc
        return 0

    def dec(self, id_, want):
        for d, a in self.decorates.get(id_, []):
            if d == want:
                return a[0]
        return None

    # --- бюджет ------------------------------------------------------------
    def budget(self):
        res = {"textures": 0, "samplers": 0, "ubo": 0, "ssbo": 0,
               "ubo_bytes": 0, "ssbo_bytes": 0, "sets": set(), "bindings": set(),
               "dyn_ubo": [], "names": [], "ssbo_detail": [], "dyn_ssbo": []}
        for vid, vname, ptype, sc in self.variables:
            base = self.pointee(ptype)
            kind, _args = self.kind_of(base)
            if kind == "other" or kind == "struct":
                continue
            if sc == SC_FUNCTION:
                continue
            setn = self.dec(vid, DEC_DESCRIPTOR_SET)
            bind = self.dec(vid, DEC_BINDING)
            if kind == "texture":
                res["textures"] += 1
            elif kind == "sampler":
                res["samplers"] += 1
            elif kind in ("ubo", "ssbo"):
                sz = self.size_of(base)
                res[kind] += 1
                res[kind + "_bytes"] += sz
                if kind == "ssbo":
                    # раскладка SSBO: элементы массива (имя, длина, ArrayStride) —
                    # это контракт для C++-писателя данных (шаг 10, §3.z п.2)
                    stop, sargs = self.types.get(base, (None, ()))
                    if stop == OP_TYPE_STRUCT:
                        for midx, mid in enumerate(sargs):
                            mop, margs = self.types.get(mid, (None, ()))
                            if mop not in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
                                continue
                            cnt = (self.const_value(margs[1])
                                   if mop == OP_TYPE_ARRAY else "runtime")
                            stride = next((a[0] for d, a in
                                           self.decorates.get(mid, [])
                                           if d == DEC_ARRAY_STRIDE), 0)
                            off = next((a[0] for d, a in
                                        self.member_decorates.get((base, midx), [])
                                        if d == DEC_OFFSET), 0)
                            res["ssbo_detail"].append(
                                (vname, self.member_name.get((base, midx), "?"),
                                 cnt, stride, off))
            if setn is not None and bind is not None:
                res["sets"].add(setn)
                res["bindings"].add((setn, bind))
            res["names"].append((sc, kind, vname, self.size_of(base), setn, bind))
        # динамическая индексация uniform-буфера: доступ к члену UBO индексом,
        # который не OpConstant. Класс указателя наследуется от базового
        # объекта (переменная или предыдущая цепочка доступа).
        # id указателя -> (storage class, id корневой переменной)
        root, var_pt = {}, {}
        for vid, _n, pt, sc in self.variables:
            root[vid] = (sc, vid)
            var_pt[vid] = pt
        for _rt, res_id, base, _idx in self.accesses:
            if base in root:
                root[res_id] = root[base]
        for res_id, base in self.loads:
            if base in root:
                root[res_id] = root[base]
        for _rt, _res, base, idxs in self.accesses:
            info = root.get(base)
            if not info or info[0] not in (SC_UNIFORM, SC_STORAGE_BUFFER) or not idxs:
                continue
            if not any(i not in self.const_ids for i in idxs):
                continue
            # класс определяем поDecoration'у корневого буфера (Block / BufferBlock),
            # а не по storage class: DXC кладёт и StructuredBuffer, и uniform
            # в storage class Uniform (2), различаются они только декорацией
            rootkind, _rk = self.kind_of(self.pointee(var_pt.get(info[1], 0)))
            if rootkind not in ("ubo", "ssbo"):
                continue
            # первый индекс — номер члена struct (он константный), по нему
            # называем массив, который индексируется динамически
            struct = self.pointee(var_pt.get(info[1], 0))
            named = "?"
            op, args = self.types.get(struct, (None, ()))
            if op == OP_TYPE_STRUCT and idxs and idxs[0] in self.const_ids:
                mi = self.const_value(idxs[0])
                if mi < len(args):
                    mt = args[mi]
                    mop, margs = self.types.get(mt, (None, ()))
                    cnt = self.const_value(margs[1]) if mop == OP_TYPE_ARRAY else "runtime"
                    stride = next((a[0] for d, a in self.decorates.get(mt, [])
                                   if d == DEC_ARRAY_STRIDE), 0)
                    named = "%s[%s x %d B]" % (self.member_name.get((struct, mi), "?"),
                                               cnt, stride)
            res["dyn_ubo" if rootkind == "ubo" else "dyn_ssbo"].append(named)
        return res


# ------------------------------------------------------------------ компиляция
def spec_materials():
    root = ET.parse(os.path.join(SH, "MaterialSpec.xml")).getroot()
    out = []
    for item in root.iter("Item"):
        name = item.findtext("shaderName")
        if not name:
            continue
        pins = [(p.findtext("pinName"), int(p.findtext("pinMultiplier", "1")))
                for p in item.iter("Item") if p.findtext("pinName")]
        out.append((name, pins))
    return out


def entries(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    vs = re.findall(r"^\s*[\w:]+\s+(\w+)_VSMain\s*\(", src, re.M)
    ps = re.findall(r"^\s*[\w:]+\s+(\w+)_PSMain\s*\(", src, re.M)
    return vs, ps


SHIM = "--no-shim" not in sys.argv
KEEP = "--keep" in sys.argv
PINVAL = int(sys.argv[sys.argv.index("--pins") + 1]) if "--pins" in sys.argv else 0
# --define NAME=ЗНАЧЕНИЕ (повторяемо): включает макросы шва исходников, например
# -DPW_SSBO_ARRAYS=1 для замера SSBO-формы динамических массивов (шаг 10)
EXTRA_DEFINES = [sys.argv[i + 1] for i, a in enumerate(sys.argv)
                 if a == "--define" and i + 1 < len(sys.argv)]
SHIM_DIR = None


def cleanup_shim_trees():
    """Сгенерированное дерево живёт внутри Src/Render (иначе DXC не находит
    stdafx.h и относительные include шейдеров) и не должно оставаться в
    рабочем дереве репозитория."""
    if KEEP:
        return
    for stage in ("vs", "ps"):
        d = os.path.join(RENDER, "zzz_budget_shaders_" + stage)
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)


atexit.register(cleanup_shim_trees)


def build_shim_tree(stage):
    names_ps, names_vs = set(), set()
    for fn in os.listdir(SH):
        if not fn.endswith(".hlsl"):
            continue
        t = open(os.path.join(SH, fn), encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"^\s*(\w+)\s+\w+_PSMain\s*\(", t, re.M):
            names_ps.add(m.group(1))
        for m in re.finditer(r"^\s*(\w+)\s+\w+_VSMain\s*\(", t, re.M):
            names_vs.add(m.group(1))
    global SHIM_DIR
    SHIM_DIR = os.path.join(RENDER, "zzz_budget_shaders_" + stage)
    os.makedirs(SHIM_DIR, exist_ok=True)
    for fn in os.listdir(SH):
        src = os.path.join(SH, fn)
        if not os.path.isfile(src):
            continue
        if fn.endswith(".hlsl"):
            out, _st, _n = shim.rewrite(
                open(src, encoding="utf-8", errors="replace").read(), stage,
                tuple(sorted(names_ps)), tuple(sorted(names_vs)))
            open(os.path.join(SHIM_DIR, fn), "w", encoding="utf-8").write(out)
        elif fn.endswith(".h"):
            open(os.path.join(SHIM_DIR, fn), "wb").write(open(src, "rb").read())
    return SHIM_DIR


def compile_unit(path, stage, entry, defines):
    if SHIM:
        path = os.path.join(SHIM_DIR, os.path.basename(path))
    inc = SHIM_DIR if SHIM else SH
    out_spv = "/tmp/shader_budget_%s_%s.spv" % (os.path.basename(path), stage)
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv",
           "-I", inc, "-I", RENDER]
    for d in defines:
        cmd += ["-D", d]
    cmd += [path, "-Fo", out_spv]
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or ""), out_spv


def main():
    args = sys.argv[1:]
    only = args[args.index("--only") + 1] if "--only" in args else None
    dump = "--dump" in args
    mats = spec_materials()
    rows, fails = [], []
    for name, pins in mats:
        if only and only != name:
            continue
        path = os.path.join(SH, name + ".hlsl")
        if not os.path.exists(path):
            continue
        vs, ps = entries(path)
        defines = ["%s=%d" % (p, PINVAL) for p, _ in pins] + EXTRA_DEFINES
        for stage, lst in (("vs", vs), ("ps", ps)):
            if not lst:
                continue
            global SHIM_DIR
            if SHIM:
                SHIM_DIR = build_shim_tree(stage)
            code, err, spv = compile_unit(path, stage,
                                          lst[0] + "_" + stage.upper() + "Main",
                                          defines)
            if code != 0:
                fails.append((name + "/" + stage, err.strip().splitlines()[:1]))
                continue
            m = Module(spv)
            b = m.budget()
            rows.append((name, stage, b))
            if dump:
                print("== %s/%s" % (name, stage))
                for sc, kind, vn, sz, setn, bind in b["names"]:
                    print("   %-14s %-8s %-28s %6d B  set=%s bind=%s"
                          % ("sc" + str(sc), kind, vn, sz, setn, bind))
                if b["dyn_ubo"]:
                    print("   ДИНАМИЧЕСКАЯ ИНДЕКСАЦИЯ UBO: %s"
                          % ", ".join(sorted(set(b["dyn_ubo"]))))
                for vn, mn, cnt, stride, off in b["ssbo_detail"]:
                    print("   SSBO %-18s %-18s [%s x %d B] offset=%d"
                          % (vn, mn, cnt, stride, off))

    if not rows:
        print("нет успешных единиц (FAIL: %d)" % len(fails))
        for k, e in fails[:10]:
            print("  %s %s" % (k, e))
        return

    def agg(key):
        return [r[2][key] for r in rows]

    print("единиц разобрано: %d (VS %d, PS %d), FAIL: %d"
          % (len(rows), sum(1 for r in rows if r[1] == "vs"),
             sum(1 for r in rows if r[1] == "ps"), len(fails)))
    allb = set()
    for _n, _s, b in rows:
        allb |= b["bindings"]
    for key, unit in (("textures", "текстур"), ("samplers", "сэмплеров"),
                      ("ubo", "UBO"), ("ssbo", "SSBO")):
        v = agg(key)
        print("  %-10s макс %3d   среднее %5.2f   максимум по всему корпусу: %d"
              % (unit, max(v), sum(v) / len(v),
                 max((b[key] for _n, _s, b in rows))))
    print("  всего различных (set,binding) по корпусу: %d   max binding: %d"
          % (len(allb), max((bn for _st, bn in allb), default=-1)))
    kinds = {}
    for _n, _s, b in rows:
        for sc, kind, _vn, _sz, setn, bind in b["names"]:
            if setn is not None:
                kinds.setdefault(kind, []).append(bind)
    for kind, bl in sorted(kinds.items()):
        print("  binding %-8s диапазон %d..%d" % (kind, min(bl), max(bl)))
    print("  UBO байт   макс %6d   среднее %8.1f" % (max(agg("ubo_bytes")),
                                                     sum(agg("ubo_bytes")) / len(rows)))
    print("  SSBO байт  макс %6d" % max(agg("ssbo_bytes")))
    maxset = max((max(b["sets"]) if b["sets"] else 0) for _n, _s, b in rows)
    print("  максимальный номер set у DXC по умолчанию: %d" % maxset)

    print("\nверхние потребители (текстуры+сэмплеры):")
    for name, stage, b in sorted(rows, key=lambda r: -(r[2]["textures"] + r[2]["samplers"]))[:12]:
        print("  %-30s %-3s tex=%d smp=%d ubo=%d(%d B) ssbo=%d(%d B) sets=%s"
              % (name, stage, b["textures"], b["samplers"], b["ubo"],
                 b["ubo_bytes"], b["ssbo"], b["ssbo_bytes"], sorted(b["sets"])))

    dyn = [(n, s, sorted(set(b["dyn_ubo"]))) for n, s, b in rows if b["dyn_ubo"]]
    print("\nединицы с НЕконстантной индексацией членов UBO: %d из %d"
          % (len(dyn), len(rows)))
    for n, s, names in dyn[:20]:
        print("  %-30s %-3s %s" % (n, s, ", ".join(names)))

    # раскладка SSBO — контракт для C++-писателя (шаг 10): элемент, его размер,
    # смещение в буфере. Сравнивается с packing'ом тех же данных в c-регистрах.
    ssbo = [(n, s, d) for n, s, b in rows for d in b["ssbo_detail"]]
    if ssbo:
        print("\nраскладка SSBO: %d буферов" % len(ssbo))
        for n, s, (vn, mn, cnt, stride, off) in ssbo:
            print("  %-30s %-3s %-14s %-18s [%s x %d B] offset=%d"
                  % (n, s, vn, mn, cnt, stride, off))
    else:
        print("\nSSBO в корпусе: нет")
    if fails:
        print("\nне компилируются: %d" % len(fails))
        for k, e in fails[:10]:
            print("  %-34s %s" % (k, e))


if __name__ == "__main__":
    main()
