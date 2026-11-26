#!/usr/bin/env python3
"""x86 ASan/UBSan builds of real installed effects, from their own source trees, for test/host_real_test.c:

    python3 test/build_hosted.py <out dir> <path/to/vst.json>...      -> <out dir>/<so>, prints "name<TAB>path" per plugin

Compiles each port the way tools/test_port.sh does (gen_vst.py --params-h, its sources and cflags + the wrapper).
"""
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

MV = os.environ.get("MPC_VST") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "mpc-vst-plugins"))
SAN = os.environ.get("SAN", "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1").split()


# ports that build with their own script, not vst.json "build" (and their own VST2 code, not the toolchain wrapper)
OWN = {"Liminal Hz": {"root": "..", "wrapper": False, "libs": ["-ldl"],
                      "sources": ["engine/pffft/pffft.c", "engine/pffft/pffft_common.c", "engine/long_convolver.cpp",
                                  "engine/reverb_engine.cpp", "engine/wav_reader.cpp", "engine/library.cpp", "engine/presets.cpp",
                                  "engine/presets_web.cpp", "engine/tone3000.cpp", "engine/http.cpp", "engine/pkce.cpp",
                                  "vst/liminal_hz_vst.cpp"],
                      "cflags": ["-std=c++17", "-Iengine", "-Iengine/pffft", "-Isrc/audiodsptools", "-Isrc/nlohmann",
                                 "-fsigned-char", "-D_FILE_OFFSET_BITS=64", "-w", "-DLIMINAL_GEN2"]}}


def build(out, cfgp):
    cfg = json.load(open(cfgp))
    if "build" not in cfg:
        cfg["build"] = OWN[cfg["name"]]
    vdir = os.path.dirname(os.path.abspath(cfgp))
    subprocess.run([sys.executable, os.path.join(MV, "tools", "gen_vst.py"), cfgp, "--params-h"], check=True,
                   stdout=subprocess.DEVNULL)
    root = os.path.normpath(os.path.join(vdir, cfg["build"].get("root", ".")))
    WR = os.environ.get("MPC_WRAPPER") or os.path.join(MV, "wrapper")   # another wrapper checkout (a port built against a newer one)
    inc = ["-I" + os.path.join(vdir, "build"), "-I" + WR]
    cflags = [f if not f.startswith("-I") else "-I" + os.path.join(root, f[2:]) for f in cfg["build"].get("cflags", [])]
    cflags = [f for f in cflags if not f.startswith("-O")]
    tag = cfg["so"].replace(".so", "")
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)

    def cc(a):
        i, src = a
        path = src if os.path.isabs(src) else os.path.join(root, src)
        o = os.path.join(out, "obj", "%s_%d.o" % (tag, i))
        if os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(path) and "vst2_wrap" not in path:
            return o
        cxx = path.endswith((".cpp", ".cc", ".cxx"))
        std = [] if any(f.startswith("-std=") for f in cflags) and cxx else ["-std=gnu++11" if cxx else "-std=gnu11"]
        fl = cflags if cxx else [f for f in cflags if not re.match(r"-std=(gnu|c)\+\+", f)]
        subprocess.run(["g++" if cxx else "gcc"] + SAN + ["-fPIC", "-fvisibility=hidden"] + std + fl + inc + ["-c", path, "-o", o],
                       check=True)
        return o
    with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        objs = list(ex.map(cc, enumerate(cfg["build"]["sources"] + ([os.path.join(WR, "vst2_wrap.c")] if cfg["build"].get("wrapper", True) else []))))
    so = os.path.join(out, cfg["so"])
    subprocess.run(["g++"] + SAN + ["-shared"] + objs + cfg["build"].get("libs", []) + ["-lpthread", "-o", so], check=True)
    print("%s\t%s" % (cfg["name"], so))


if __name__ == "__main__":
    for c in sys.argv[2:]:
        build(sys.argv[1], c)
