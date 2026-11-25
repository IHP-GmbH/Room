# Measure KLayout open -> hierarchy shown -> zoom_fit -> event pump until draw settles.
# Usage (GUI, not -b/-zz):
#   klayout.exe -rd FILE=path -rd OUT=timing.txt -rd SETTLE_MS=1500 -r bench_klayout_open_draw.py
#
# Prints KEY=value lines and writes the same to OUT.

import os
import sys
import time

import pya


def env(name, default=""):
    try:
        v = globals().get(name)
        if v is not None and str(v) != "":
            return str(v)
    except Exception:
        pass
    return os.environ.get(name, default)


def write_result(path, lines):
    text = "\n".join(lines) + "\n"
    print(text, end="")
    sys.stdout.flush()
    if path:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)


def main():
    path = env("FILE")
    out = env("OUT")
    settle_ms = int(env("SETTLE_MS", "1500") or "1500")
    if not path:
        write_result(out, ["error=missing FILE"])
        pya.Application.instance().exit(2)
        return

    path = os.path.abspath(path)
    app = pya.Application.instance()
    mw = app.main_window()

    lines = [
        "file=" + path,
        "bytes=" + str(os.path.getsize(path) if os.path.isfile(path) else -1),
    ]

    t0 = time.perf_counter()
    try:
        mw.load_layout(path, 0)
    except Exception as ex:
        write_result(out, lines + ["error=load_layout: " + str(ex), "ok=0"])
        try:
            app.exit(3)
        except Exception:
            pass
        return
    t_load = time.perf_counter()

    lv = mw.current_view()
    if lv is None:
        write_result(out, lines + ["error=no current view", "ok=0"])
        app.exit(4)
        return

    try:
        lv.max_hier()
    except Exception:
        pass
    try:
        lv.zoom_fit()
    except Exception:
        pass
    try:
        lv.update_content()
    except Exception:
        pass

    # Pump Qt events until settle_ms of continuous processing after fit.
    # Approximation of "drawing finished" for interactive open.
    settle_s = max(0.2, settle_ms / 1000.0)
    t_pump0 = time.perf_counter()
    while (time.perf_counter() - t_pump0) < settle_s:
        app.process_events()
    try:
        lv.update_content()
    except Exception:
        pass
    t_end = time.perf_counter()
    # Extra short pump after final update
    t_extra0 = time.perf_counter()
    while (time.perf_counter() - t_extra0) < 0.25:
        app.process_events()
    t_draw = time.perf_counter()

    ly = lv.active_cellview().layout() if lv.active_cellview() else None
    cells = ly.cells() if ly is not None else -1
    top = ""
    try:
        if ly is not None and ly.top_cell() is not None:
            top = ly.top_cell().name
    except Exception:
        pass

    lines.extend(
        [
            "cells=" + str(cells),
            "top=" + top,
            "load_s=" + ("%.3f" % (t_load - t0)),
            "draw_s=" + ("%.3f" % (t_draw - t0)),
            "settle_ms=" + str(settle_ms),
            "ok=1",
        ]
    )
    write_result(out, lines)

    try:
        mw.close_all()
    except Exception:
        pass
    app.exit(0)


main()
