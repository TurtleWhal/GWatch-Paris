#!/usr/bin/env sh
# Build the simulator as a static HTML/JS/WASM bundle for browsers.
#
# Usage:
#   simulator/web/build.sh            # one-shot build
#   simulator/web/build.sh --watch    # build, serve, rebuild on save
#
# Output:
#   simulator/build-web/gwatch_sim.{html,js,wasm}
#
# Drop those three files on any static host (GitHub Pages, Vercel, Netlify,
# python -m http.server). The HTML shell already wires up the touch
# canvas + sidebar control buttons via Module.ccall.
#
# Requires the Emscripten SDK on PATH (emcc, emcmake, em++). See
# https://emscripten.org/docs/getting_started/downloads.html or one-liner:
#   git clone https://github.com/emscripten-core/emsdk
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
#
# Watch mode additionally needs fswatch: brew install fswatch

set -e

SIM_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_ROOT="$(cd "$SIM_DIR/.." && pwd)"
BUILD_DIR="$SIM_DIR/build-web"
PORT="${PORT:-8000}"

cd "$SIM_DIR"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "emcmake not on PATH. Activate the Emscripten SDK first:"
    echo "    source /path/to/emsdk/emsdk_env.sh"
    exit 1
fi

build() {
    # First-time configure if the build dir doesn't exist yet.
    if [ ! -d "$BUILD_DIR" ]; then
        emcmake cmake -B "$BUILD_DIR" -S "$SIM_DIR"
        # First build single-job: emcc's bundled SDL2 port races with -j
        # when its sysroot cache is empty. Subsequent builds parallel-safe.
        cmake --build "$BUILD_DIR"
    else
        cmake --build "$BUILD_DIR" -j
    fi
}

if [ "$1" = "--watch" ] || [ "$1" = "-w" ]; then
    if ! command -v fswatch >/dev/null 2>&1; then
        echo "fswatch required for --watch mode. Install with:"
        echo "    brew install fswatch"
        exit 1
    fi

    build

    # Background http.server bound to the build output. Killed on exit
    # via the EXIT trap.
    (cd "$BUILD_DIR" && python3 -m http.server "$PORT" >/dev/null 2>&1) &
    SERVER_PID=$!
    trap "kill $SERVER_PID 2>/dev/null; exit" INT TERM EXIT

    echo
    echo "Serving http://localhost:$PORT/gwatch_sim.html (server pid $SERVER_PID)"
    echo "Watching for changes — Ctrl-C to stop."
    echo

    # `--latency 0.3` debounces rapid saves; -e cuts events down to types
    # that matter for source files. Watching main/ for the UI tree and
    # simulator/ for everything else; intentionally excluding generated
    # font/image sources from triggering rebuilds (they rarely change
    # and are slow to relink). The pipeline runs cmake on every event;
    # `cmake --build` no-ops if nothing's actually stale, so the cost of
    # a spurious event is ~50 ms.
    fswatch --latency 0.3 -o \
        --exclude '/build($|/)' \
        --exclude '/build-web($|/)' \
        --exclude '/managed_components($|/)' \
        --exclude '\.git($|/)' \
        --exclude '/\.DS_Store$' \
        "$PROJECT_ROOT/main" \
        "$SIM_DIR/main.cpp" \
        "$SIM_DIR/src" \
        "$SIM_DIR/shim" \
        "$SIM_DIR/lv_conf.h" \
        "$SIM_DIR/web/shell.html" \
        "$SIM_DIR/CMakeLists.txt" |
    while read -r _; do
        echo "[$(date +%H:%M:%S)] change detected — rebuilding…"
        if cmake --build "$BUILD_DIR" -j; then
            echo "[$(date +%H:%M:%S)] ok — reload the browser."
        else
            echo "[$(date +%H:%M:%S)] build failed."
        fi
    done
else
    build

    echo
    echo "Built:"
    ls -la "$BUILD_DIR"/gwatch_sim.html "$BUILD_DIR"/gwatch_sim.js "$BUILD_DIR"/gwatch_sim.wasm 2>/dev/null

    echo
    echo "To preview locally:"
    echo "    cd $BUILD_DIR && python3 -m http.server $PORT"
    echo "    open http://localhost:$PORT/gwatch_sim.html"
    echo
    echo "Or for auto-rebuild on save:"
    echo "    simulator/web/build.sh --watch"
fi
