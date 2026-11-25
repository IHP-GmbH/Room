# Per-cell local geometry XOR of two layouts (GDS/OAS/ROOM).
# Compares each matching cell's own shapes (not deep-flattened hierarchy).
# Usage:
#   klayout -b -rd A=a.oas -rd B=b.layout.room -rd OUT=xor.txt -r bench_klayout_xor.py
#
# Exit: 0 identical, 1 differences, 2 error

import os
import sys
import time

import pya


def g(name, default=""):
    try:
        v = globals().get(name)
        if v is not None and str(v) != "":
            return str(v)
    except Exception:
        pass
    return os.environ.get(name, default)


def layer_key(ly, li):
    info = ly.get_info(li)
    return (info.layer, info.datatype, info.name or "")


def main():
    path_a = os.path.abspath(g("A"))
    path_b = os.path.abspath(g("B"))
    out = g("OUT")
    skip_text = g("SKIP_TEXT", "0") in ("1", "true", "True", "yes")
    max_report = int(g("MAX_REPORT", "40") or "40")

    lines = []

    def emit(s):
        lines.append(s)
        print(s)
        sys.stdout.flush()

    emit("A=" + path_a)
    emit("B=" + path_b)
    if not os.path.isfile(path_a) or not os.path.isfile(path_b):
        emit("error=missing input file")
        emit("ok=0")
        if out:
            open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        return 2

    t0 = time.perf_counter()
    lya = pya.Layout()
    lyb = pya.Layout()
    try:
        lya.read(path_a)
        t_a = time.perf_counter()
        lyb.read(path_b)
        t_b = time.perf_counter()
    except Exception as ex:
        emit("error=read: " + str(ex))
        emit("ok=0")
        if out:
            open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        return 2

    emit("read_a_s=%.3f" % (t_a - t0))
    emit("read_b_s=%.3f" % (t_b - t_a))
    emit("cells_a=%d" % lya.cells())
    emit("cells_b=%d" % lyb.cells())

    names_a = sorted([c.name for c in lya.each_cell()])
    names_b = sorted([c.name for c in lyb.each_cell()])
    only_a = sorted(set(names_a) - set(names_b))
    only_b = sorted(set(names_b) - set(names_a))
    if only_a:
        emit("only_in_a=%d" % len(only_a))
        for n in only_a[:20]:
            emit("only_a_cell=" + n)
    if only_b:
        emit("only_in_b=%d" % len(only_b))
        for n in only_b[:20]:
            emit("only_b_cell=" + n)

    common = sorted(set(names_a) & set(names_b))
    emit("common_cells=%d" % len(common))

    keys = {}
    for li in lya.layer_indices():
        keys[layer_key(lya, li)] = True
    for li in lyb.layer_indices():
        keys[layer_key(lyb, li)] = True
    layer_list = sorted(keys.keys(), key=lambda k: (k[0], k[1], k[2]))
    emit("layers=%d" % len(layer_list))

    total = 0
    differing_pairs = 0
    reported = 0
    t_xor0 = time.perf_counter()
    for cell_name in common:
        ca = lya.cell(cell_name)
        cb = lyb.cell(cell_name)
        for (layer, datatype, name) in layer_list:
            if skip_text and layer == 8 and datatype == 25:
                continue
            info = pya.LayerInfo(layer, datatype)
            if name:
                info.name = name
            lia = lya.layer(info)
            lib = lyb.layer(info)
            # Local shapes only (same as paper per-cell XOR).
            ra = pya.Region(ca.shapes(lia))
            rb = pya.Region(cb.shapes(lib))
            xor = ra ^ rb
            n = xor.count()
            if n == 0:
                continue
            total += n
            differing_pairs += 1
            if reported < max_report:
                label = "%d/%d" % (layer, datatype)
                if name:
                    label += " (%s)" % name
                emit("diff=%s cell=%s shapes=%d" % (label, cell_name, n))
                reported += 1

    t_xor1 = time.perf_counter()
    emit("xor_s=%.3f" % (t_xor1 - t_xor0))
    emit("differing_cell_layer_pairs=%d" % differing_pairs)
    emit("xor_shapes=%d" % total)
    cell_list_ok = 1 if (not only_a and not only_b) else 0
    emit("cell_lists_match=%d" % cell_list_ok)
    identical = 1 if (total == 0 and cell_list_ok) else 0
    emit("identical=%d" % identical)
    emit("ok=1")
    if out:
        open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    return 0 if identical else 1


sys.exit(main())
