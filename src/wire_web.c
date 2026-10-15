/* wire_web.c - Wire compiled to WebAssembly, for the playground.
 *
 * The whole compiler, with main() renamed and one function to call instead:
 *
 *   wire_run("--to-vhdl\n/work/adder.wire")   arguments, one per line
 *
 * returns what main() returns. Files come from the Emscripten file system
 * the page fills in (the circuit, the files it imports, and the standard
 * library in /wire/lib, found as by an installed wire in /wire/bin); stdout
 * and stderr go to the page. wire.c calls exit()
 * on the first error, which ends the instance, so the page makes a fresh
 * instance for every run. Built by `make web`. */
#include <emscripten.h>

#define main wire_main
#include "wire.c"
#undef main

EMSCRIPTEN_KEEPALIVE int wire_run(const char *args)
{
    static char buf[1 << 16];
    char *argv[64];
    int argc = 0;
    char *p = buf;
    argv[argc++] = "/wire/bin/wire";   /* the standard library is in /wire/lib, as when installed */
    snprintf(buf, sizeof buf, "%s", args);
    while (*p && argc < 63) {
        argv[argc++] = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    return wire_main(argc, argv);
}
