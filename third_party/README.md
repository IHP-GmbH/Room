# Third-party dependencies

## Cap'n Proto (`capnp-install/`)

CommonDB uses a local Cap'n Proto install under `third_party/capnp-install/` (compiler, headers, `libcapnp` / `libkj`).

**Normal workflow:** run `cmake` from the repo root. If `capnp-install` is missing, CMake runs the bootstrap script automatically (`COMMONDB_BOOTSTRAP_CAPNP=ON` by default). The first configure may take several minutes; Git and your C++ toolchain (e.g. MinGW `gcc` / `mingw32-make` on `PATH`, or paths passed via `-DCMAKE_C_COMPILER` / `-DCMAKE_MAKE_PROGRAM`) must be available.

**Manual build** (optional):

**Windows:**

```bat
scripts\mkcapnp.cmd
```

**Linux / macOS:**

```bash
chmod +x scripts/mkcapnp.sh scripts/build_capnp_linux.sh
./scripts/mkcapnp.sh
```

The Cap'n Proto source clone is stored in `third_party/capnproto/` (gitignored). Override the install prefix with `-DCAPNP_ROOT=...` when configuring CMake. Disable auto-bootstrap with `-DCOMMONDB_BOOTSTRAP_CAPNP=OFF` if you maintain `capnp-install` yourself.
