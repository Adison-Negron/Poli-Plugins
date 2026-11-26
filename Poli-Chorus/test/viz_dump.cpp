// prints the display columns the engine would send for a setting: viz_dump <mode 0..2> <rate pos> <amount pos> <width pos>
// line 1: the left (black) frames, line 2: the right (red) frames. Used by preview.py and the tests, so the preview shows what the engine draws.
#include "ChoViz.h"
#include <cstdio>
#include <cstdlib>
using namespace cho;
int main(int argc, char **argv) {
    if (argc < 5) return 1;
    const int mode = atoi(argv[1]);
    const float r = (float)atof(argv[2]), a = (float)atof(argv[3]), w = (float)atof(argv[4]);
    for (int side = 0; side < 2; ++side) {
        for (int j = 0; j < kVizCols; ++j) printf("%d%c", vizFrame(mode, r, a, w, j, side == 1), j + 1 < kVizCols ? ' ' : '\n');
    }
    return 0;
}
