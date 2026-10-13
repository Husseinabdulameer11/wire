/* wire - a language for digital circuits, built with gram.
 *
 *   wire circuit.wire              writes circuit.html
 *   wire -o page.html circuit.wire
 *   wire --json circuit.wire       prints the netlist
 *
 * Files are loaded first, following `import`s, and every `component` is
 * collected. Then each scope (the circuit, and every use of a component)
 * is read in three passes:
 *   1. collect   every name (declarations and assignment targets) gets a net
 *   2. widths    bit widths are inferred, so only inputs and ports need `[n]`
 *   3. build     expressions become parts (nodes) and nets
 * A component is expanded where it is used, as an instance: its ports
 * become pin nodes at the boundary, so the netlist stays flat for the
 * simulator while the page can still show each instance as a chip and open
 * it. Slices of one bus are grouped into a splitter. The netlist is written
 * as JSON into a copy of the viewer page (wire_view.html). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gram.h"
#include "wire_grammar.h"
#include "wire_view.h"

#ifndef WIRE_VERSION
#define WIRE_VERSION "dev"
#endif

#define MAXW   32   /* widest bus */
#define MAXIO  40   /* most inputs or outputs on one component */
#define NAMEW  32   /* longest port name */

/* ---- files ------------------------------------------------------------- */

#define MAXFILES 64
typedef struct { const char *name; const char *text; size_t len; } File;
static File files[MAXFILES];
static int nfiles;
static const char *src_name;   /* the file named on the command line */

static int file_of(const gram_node *at)
{
    int i;
    for (i = 0; at && i < nfiles; i++)
        if (at->text >= files[i].text && at->text <= files[i].text + files[i].len) return i;
    return 0;
}

/* ---- errors ------------------------------------------------------------ */

static void fail(const gram_node *at, const char *f, ...)
{
    va_list ap;
    size_t line = 0, col = 0;
    File *fl = &files[file_of(at)];
    if (at) gram_locate(fl->text, fl->len, (size_t)(at->text - fl->text), &line, &col);
    fprintf(stderr, "%s:", fl->name);
    if (at) fprintf(stderr, "%lu:%lu:", (unsigned long)line, (unsigned long)col);
    fputs(" error: ", stderr);
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static int line_of(const gram_node *at)
{
    size_t line = 0;
    File *fl;
    if (!at) return 0;
    fl = &files[file_of(at)];
    gram_locate(fl->text, fl->len, (size_t)(at->text - fl->text), &line, NULL);
    return (int)line;
}

static void *grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap) return p;
    *cap = *cap ? *cap * 2 : 64;
    if (*cap < need) *cap = need;
    p = realloc(p, (size_t)*cap * size);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static const char *plural(int n) { return n == 1 ? "" : "s"; }

/* ---- nets -------------------------------------------------------------- */

typedef struct {
    int w;         /* bits, 0 = not known yet */
    int parent;    /* union-find */
    int drivers;   /* components writing it */
    int readers;   /* components reading it */
    const char *name; int nlen;
    const gram_node *at;
} Net;

static Net *nets; static int nnets, capnets;

static int new_net(int w)
{
    nets = (Net *)grow(nets, &capnets, nnets + 1, sizeof *nets);
    memset(&nets[nnets], 0, sizeof *nets);
    nets[nnets].w = w;
    nets[nnets].parent = nnets;
    return nnets++;
}

static int find(int n)
{
    while (nets[n].parent != n) n = nets[n].parent = nets[nets[n].parent].parent;
    return n;
}

/* ---- components -------------------------------------------------------- */

enum {
    K_AND, K_OR, K_NAND, K_NOR, K_XOR, K_XNOR, K_NOT, K_BUF,
    K_MUX, K_DEMUX, K_DECODER, K_ENCODER, K_ADD, K_COMPARE,
    K_DFF, K_TFF, K_JKFF, K_COUNTER,
    K_TRI, K_ROM, K_RAM,
    /* not callable: */
    K_INPUT, K_BUTTON, K_CLOCK, K_KEYPAD, K_OUTPUT, K_LED, K_SEG7, K_HEX,
    K_CONST, K_SPLIT, K_MERGE,
    K_BUS,                        /* joins the tri() outputs that drive one bus */
    K_PIN_IN, K_PIN_OUT,          /* the edge of an instance */
    K_USER,                       /* a call of a component written in Wire */
    K_COUNT
};

static const char *kind_names[K_COUNT] = {
    "and", "or", "nand", "nor", "xor", "xnor", "not", "buf",
    "mux", "demux", "decoder", "encoder", "add", "compare",
    "dff", "tff", "jkff", "counter",
    "tri", "rom", "ram",
    "input", "button", "clock", "keypad", "output", "led", "seg7", "hex",
    "const", "split", "merge", "bus", "pin_in", "pin_out", "component"
};

typedef struct {
    int kind;
    const char *name; int nlen;   /* label: the name it was assigned to */
    int line;
    int nin, nout;
    int in[MAXIO], out[MAXIO];
    char inname[MAXIO][NAMEW], outname[MAXIO][NAMEW];
    int range[MAXIO][2];          /* split: per output, merge: per input */
    int inst, file;               /* which instance it belongs to; its source file */
    unsigned long value;          /* const */
    int bits;                     /* counter */
    const char *opt; int optlen;  /* led colour */
    long hz;                      /* clock, in hundredths */
    int membits, memabits;        /* rom, ram: bits per word, address bits */
    unsigned long *mem; long memlen; /* initial contents: words 0..memlen-1 */
    int dead;                     /* merged into an identical gate */
} Node;

static Node *nodes; static int nnodes, capnodes;
static int cur_inst;              /* instance being built, 0 = the circuit */

/* instances: where each use of a component sits (0 is the circuit itself) */
typedef struct { int comp, parent; const char *name; int nlen; int line, file; } Inst;
static Inst *insts; static int ninsts, capinsts;

static Node *new_node(int kind, const gram_node *at)
{
    Node *n;
    nodes = (Node *)grow(nodes, &capnodes, nnodes + 1, sizeof *nodes);
    n = &nodes[nnodes++];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->line = line_of(at);
    n->file = file_of(at);
    n->inst = cur_inst;
    return n;
}

static void node_in(Node *n, const char *name, int net)
{
    strcpy(n->inname[n->nin], name);
    n->in[n->nin++] = net;
}

static void node_out(Node *n, const char *name, int net)
{
    strcpy(n->outname[n->nout], name);
    n->out[n->nout++] = net;
}

/* ---- names ------------------------------------------------------------- */

enum { D_NONE, D_INPUT, D_OUTPUT, D_WIRE, D_LED, D_SEG7, D_HEX, D_BUTTON, D_CLOCK,
       D_BUS, D_KEYPAD,
       D_PIN_IN, D_PIN_OUT };   /* ports, inside a component */
static const char *dkind_names[] = {
    "", "input", "output", "wire", "led", "seg7", "hex", "button", "clock", "bus", "keypad",
    "input", "output"
};

typedef struct Part { int hi, lo; gram_node *expr, *at; int net; } Part;

typedef struct {
    const char *name; int len;
    int dkind, dw;            /* declaration and declared width (0 = infer) */
    gram_node *decl, *opts;
    gram_node *expr, *at;     /* `name = expr` (whole) */
    int pre;                  /* net already built (connect with fan-out) */
    int conn;                 /* given its value by `expr -> name` */
    Part *parts; int nparts, capparts;
    int net, w, state;        /* state: 0 new, 1 inferring, 2 done */
} Sym;

/* `c.cout`, `dm[2]`: output ports of a named component, created on first use */
typedef struct { void *sym; char port[NAMEW]; int net; gram_node *at; } PortRef;

/* the names of one scope: the circuit, or one instance of a component */
typedef struct {
    Sym *syms; int nsyms, capsyms;
    PortRef *prefs; int nprefs, capprefs;
    int comp;                     /* -1 for the circuit */
} Scope;
static Scope top_scope = { 0, 0, 0, 0, 0, 0, -1 };
static Scope *sc = &top_scope;

static int same(const gram_node *id, const char *s, int len)
{
    return (int)id->len == len && !memcmp(id->text, s, (size_t)len);
}

static Sym *lookup(const gram_node *id)
{
    int i;
    for (i = 0; i < sc->nsyms; i++)
        if (same(id, sc->syms[i].name, sc->syms[i].len)) return &sc->syms[i];
    return NULL;
}

static Sym *intern(gram_node *id)
{
    Sym *s = lookup(id);
    if (s) return s;
    sc->syms = (Sym *)grow(sc->syms, &sc->capsyms, sc->nsyms + 1, sizeof *sc->syms);
    s = &sc->syms[sc->nsyms++];
    memset(s, 0, sizeof *s);
    s->name = id->text;
    s->len = (int)id->len;
    s->net = -1;
    s->pre = -1;
    return s;
}

#define NAME(s) (s)->len, (s)->name
#define TEXT(n) (int)(n)->len, (n)->text


static gram_node *sym_call(Sym *s);
static int is_default_port(gram_node *call, const char *port);

static int port_net(Sym *s, const char *port, gram_node *at)
{
    int i;
    if (sym_call(s) && is_default_port(sym_call(s), port)) return s->net;
    for (i = 0; i < sc->nprefs; i++)
        if (sc->prefs[i].sym == (void *)s && !strcmp(sc->prefs[i].port, port)) return sc->prefs[i].net;
    sc->prefs = (PortRef *)grow(sc->prefs, &sc->capprefs, sc->nprefs + 1, sizeof *sc->prefs);
    sc->prefs[sc->nprefs].sym = s;
    strcpy(sc->prefs[sc->nprefs].port, port);
    sc->prefs[sc->nprefs].net = new_net(0);
    sc->prefs[sc->nprefs].at = at;
    return sc->prefs[sc->nprefs++].net;
}

/* ---- numbers ----------------------------------------------------------- */

/* value of a literal, and its width if it fixes one (0b101 is 3 bits,
 * 0xF is 4, decimal takes the width it is used at) */
static unsigned long literal(const gram_node *n, int *fixed)
{
    unsigned long v = 0;
    size_t i = 0;
    int digits = 0, base = 10, d, over = 0;
    if (n->len > 2 && n->text[0] == '0' && (n->text[1] == 'b' || n->text[1] == 'x')) {
        base = n->text[1] == 'b' ? 2 : 16;
        i = 2;
    }
    for (; i < n->len; i++) {
        char c = n->text[i];
        if (c == '_') continue;
        d = c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0';
        if (v > (0xFFFFFFFFUL - (unsigned long)d) / (unsigned long)base) over = 1;
        v = v * (unsigned long)base + (unsigned long)d;
        digits++;
    }
    if (over) fail(n, "%.*s does not fit in %d bits", TEXT(n), MAXW);
    *fixed = base == 2 ? digits : base == 16 ? digits * 4 : 0;
    if (*fixed > MAXW) fail(n, "%.*s is wider than %d bits", TEXT(n), MAXW);
    return v & 0xFFFFFFFFUL;
}

static int bits_for(unsigned long v)
{
    int b = 1;
    while (b < MAXW && (v >> b)) b++;
    return b;
}

static int number(const gram_node *n)
{
    int fixed;
    unsigned long v = literal(n, &fixed);
    if (fixed || v > 64) fail(n, "expected a small number here");
    return (int)v;
}

/* ---- calls ------------------------------------------------------------- */

typedef struct {
    int kind;
    gram_node *call, *id;
    int npos; gram_node *pos[MAXIO];
    gram_node *sel, *en, *rst, *cin;
    gram_node *load, *d, *down;    /* counter */
    gram_node *data;               /* rom, ram: the `data = "..."` or `file = "..."` setting */
    int bits;
    /* computed: */
    int nin; gram_node *in[MAXIO]; char inname[MAXIO][NAMEW]; int inw[MAXIO];
    int nout; char outname[MAXIO][NAMEW]; int outw[MAXIO];
    int comp;              /* K_USER: which component */
    const char *kname;     /* name for messages */
    int defport;   /* output used when the component is used as a value, -1 none */
    int indexed;   /* outputs o0..oN: `x[i]` picks one */
} Call;

static int ew(gram_node *e);
static int sym_width(Sym *s);

/* ---- components written in Wire ---------------------------------------- */

typedef struct {
    char *name;
    gram_node *node, *id, *body;
    int nin, nout;
    gram_node *inid[MAXIO], *outid[MAXIO];
    int inw[MAXIO], outw[MAXIO];
    char inname[MAXIO][NAMEW], outname[MAXIO][NAMEW];
    int used;
} Comp;
#define MAXCOMPS 512
static Comp comps[MAXCOMPS];
static int ncomps;

static int comp_find(const gram_node *id)
{
    int i;
    for (i = 0; i < ncomps; i++)
        if ((int)id->len == (int)strlen(comps[i].name) && !memcmp(id->text, comps[i].name, id->len)) return i;
    return -1;
}

static int kind_of(const gram_node *id)
{
    int k;
    for (k = 0; k < K_INPUT; k++)
        if (same(id, kind_names[k], (int)strlen(kind_names[k]))) return k;
    if (same(id, "reg", 3)) return K_DFF;
    return -1;
}

static void say_kinds(char *buf)
{
    int k;
    buf[0] = 0;
    for (k = 0; k < K_INPUT; k++) {
        strcat(buf, kind_names[k]);
        strcat(buf, k + 1 < K_INPUT ? ", " : "");
    }
}

static int is_const(const gram_node *e)
{
    int fixed;
    if (e->rule != WIRE_R_number) return 0;
    literal(e, &fixed);
    return !fixed;
}

static void add_in(Call *c, gram_node *e, const char *name, int w)
{
    c->in[c->nin] = e;
    strcpy(c->inname[c->nin], name);
    c->inw[c->nin++] = w;
}

static void add_out(Call *c, const char *name, int w)
{
    strcpy(c->outname[c->nout], name);
    c->outw[c->nout++] = w;
}

static int log2_exact(int n)
{
    int s = 0;
    while ((1 << s) < n) s++;
    return (1 << s) == n ? s : -1;
}

static void need_args(Call *c, int lo, int hi, const char *how)
{
    if (c->npos < lo || c->npos > hi)
        fail(c->call, "%s takes %s, not %d", c->kname, how, c->npos);
}

/* the width an input will have; 0 if it adapts (a decimal constant) */
static int argw(gram_node *e) { return is_const(e) ? 0 : ew(e); }

static void want_width(Call *c, gram_node *e, int w, const char *what)
{
    int aw;
    if (!e || !w) return;
    aw = argw(e);
    if (aw && aw != w)
        fail(e, "%s of %s must be %d bit%s, but %.*s is %d bit%s",
             what, c->kname, w, plural(w), TEXT(e), aw, plural(aw));
}

/* a component written in Wire: its ports fix every width */
static void analyze_user(Call *c, int check)
{
    Comp *cp = &comps[c->comp];
    gram_node *a, *by[MAXIO];
    int i, npos = 0, named = 0;
    char what[NAMEW + 16];
    c->kind = K_USER;
    c->kname = cp->name;
    memset(by, 0, sizeof by);
    for (a = c->id->next; a; a = a->next) {
        if (a->rule == WIRE_R_named) {
            gram_node *k = a->child;
            if (k->next->rule == WIRE_R_string) fail(k->next, "inputs take wires or numbers, not text in quotes");
            for (i = 0; i < cp->nin; i++)
                if (same(k, cp->inname[i], (int)strlen(cp->inname[i]))) break;
            if (i == cp->nin) fail(k, "%s has no input called %.*s", cp->name, TEXT(k));
            if (by[i]) fail(k, "input %s of %s is given twice", cp->inname[i], cp->name);
            by[i] = k->next;
            named = 1;
        } else {
            if (named) fail(a, "put inputs by position before named ones");
            if (npos >= cp->nin)
                fail(a, "%s has %d input%s, but more are given here", cp->name, cp->nin, plural(cp->nin));
            by[npos++] = a;
        }
    }
    for (i = 0; i < cp->nin; i++) {
        if (!by[i]) fail(c->call, "input %s of %s is not connected", cp->inname[i], cp->name);
        sprintf(what, "input %s", cp->inname[i]);
        if (check) want_width(c, by[i], cp->inw[i], what);
        add_in(c, by[i], cp->inname[i], cp->inw[i]);
    }
    for (i = 0; i < cp->nout; i++) add_out(c, cp->outname[i], cp->outw[i]);
    c->defport = cp->nout ? 0 : -1;
}

/* look at a call: its arguments, and the widths of its inputs and outputs.
 * With `check`, mismatches are errors; without, widths may still be 0. */
static void analyze(gram_node *call, Call *c, int check)
{
    gram_node *a;
    int i, w = 0, s;
    char buf[300];
    memset(c, 0, sizeof *c);
    c->call = call;
    c->id = call->child;
    c->kind = kind_of(c->id);
    c->defport = 0;
    if (c->kind < 0 && (c->comp = comp_find(c->id)) >= 0) {
        analyze_user(c, check);
        return;
    }
    if (c->kind < 0) {
        say_kinds(buf);
        fail(c->id, "there is no part or component called %.*s. Built in: %s.%s", TEXT(c->id), buf,
             ncomps ? "" : " Components you write (or import) can be used too.");
    }
    c->kname = kind_names[c->kind];
    for (a = c->id->next; a; a = a->next) {
        if (a->rule == WIRE_R_named) {
            gram_node *k = a->child, *v = a->child->next;
            int text = same(k, "data", 4) || same(k, "file", 4);
            if (v->rule == WIRE_R_string && !text)
                fail(v, "%.*s takes a wire or a number, not text in quotes", TEXT(k));
            if (text && (c->kind == K_ROM || c->kind == K_RAM)) {
                if (v->rule != WIRE_R_string)
                    fail(v, "%.*s is text in quotes: %.*s = \"%s\"", TEXT(k), TEXT(k),
                         same(k, "file", 4) ? "program.hex" : "3E 05 76");
                if (c->data) fail(k, "give a %s either data = or file =, not both", c->kname);
                c->data = a;
            } else if (same(k, "sel", 3) && (c->kind == K_MUX || c->kind == K_DEMUX)) c->sel = v;
            else if (same(k, "en", 2) && (c->kind == K_DECODER || c->kind == K_DFF || c->kind == K_COUNTER)) c->en = v;
            else if (same(k, "rst", 3) && c->kind >= K_DFF && c->kind <= K_COUNTER) c->rst = v;
            else if (same(k, "cin", 3) && c->kind == K_ADD) c->cin = v;
            else if (same(k, "load", 4) && c->kind == K_COUNTER) c->load = v;
            else if (same(k, "d", 1) && c->kind == K_COUNTER) c->d = v;
            else if (same(k, "down", 4) && c->kind == K_COUNTER) c->down = v;
            else if (same(k, "bits", 4) && (c->kind == K_COUNTER || c->kind == K_ROM)) {
                if (v->rule != WIRE_R_number) fail(v, "bits must be a number");
                c->bits = number(v);
                if (c->bits < 1 || c->bits > MAXW) fail(v, "a %s has 1 to %d bits", c->kname, MAXW);
            } else fail(k, "%s has no setting called %.*s", c->kname, TEXT(k));
        } else {
            if (c->npos >= MAXIO - 2) fail(a, "too many inputs");
            c->pos[c->npos++] = a;
        }
    }

    switch (c->kind) {
    case K_NOT: case K_BUF:
        need_args(c, 1, 1, "one input");
        w = argw(c->pos[0]);
        add_in(c, c->pos[0], "in", w);
        add_out(c, "out", w);
        break;
    case K_AND: case K_OR: case K_NAND: case K_NOR: case K_XOR: case K_XNOR:
        need_args(c, 2, 16, "2 to 16 inputs");
        for (i = 0; i < c->npos; i++) { int aw = argw(c->pos[i]); if (aw > w) w = aw; }
        for (i = 0; i < c->npos; i++) {
            if (check) want_width(c, c->pos[i], w, "every input");
            add_in(c, c->pos[i], "", w);
        }
        add_out(c, "out", w);
        break;
    case K_MUX:
        need_args(c, 2, 32, "2 to 32 data inputs and sel=");
        if (!c->sel) fail(c->call, "mux needs a select input: mux(a, b, sel=s)");
        for (i = 0; i < c->npos; i++) { int aw = argw(c->pos[i]); if (aw > w) w = aw; }
        s = log2_exact(c->npos);
        if (s < 0) fail(c->call, "a mux has 2, 4, 8, 16 or 32 data inputs, not %d", c->npos);
        if (check) want_width(c, c->sel, s, "sel");
        for (i = 0; i < c->npos; i++) {
            char nm[8];
            if (check) want_width(c, c->pos[i], w, "every data input");
            sprintf(nm, "d%d", i);
            add_in(c, c->pos[i], nm, w);
        }
        add_in(c, c->sel, "sel", s);
        add_out(c, "out", w);
        break;
    case K_DEMUX:
        need_args(c, 1, 1, "one data input and sel=");
        if (!c->sel) fail(c->call, "demux needs a select input: demux(x, sel=s)");
        w = argw(c->pos[0]);
        s = argw(c->sel);
        if (check && (s < 1 || s > 5)) fail(c->sel, "sel of demux must be 1 to 5 bits");
        add_in(c, c->pos[0], "d", w);
        add_in(c, c->sel, "sel", s);
        if (s < 1) s = 1;
        if (s > 5) s = 5;
        for (i = 0; i < (1 << s); i++) { char nm[8]; sprintf(nm, "o%d", i); add_out(c, nm, w); }
        c->defport = -1;
        c->indexed = 1;
        break;
    case K_DECODER:
        need_args(c, 1, 1, "one select input");
        s = argw(c->pos[0]);
        if (check && (s < 1 || s > 5)) fail(c->pos[0], "the input of decoder must be 1 to 5 bits");
        if (s < 1) s = 1;
        if (s > 5) s = 5;
        add_in(c, c->pos[0], "sel", s);
        if (c->en) { if (check) want_width(c, c->en, 1, "en"); add_in(c, c->en, "en", 1); }
        add_out(c, "out", 1 << s);
        break;
    case K_ENCODER:
        need_args(c, 1, 1, "one input bus");
        w = argw(c->pos[0]);
        s = log2_exact(w);
        if (check && (s < 1)) fail(c->pos[0], "the input of encoder must be 2, 4, 8, 16 or 32 bits");
        if (s < 1) s = 1;
        add_in(c, c->pos[0], "in", w);
        add_out(c, "out", s);
        add_out(c, "valid", 1);
        break;
    case K_ADD:
        need_args(c, 2, 2, "two inputs");
        w = argw(c->pos[0]);
        if (argw(c->pos[1]) > w) w = argw(c->pos[1]);
        if (check) { want_width(c, c->pos[0], w, "both inputs"); want_width(c, c->pos[1], w, "both inputs"); }
        add_in(c, c->pos[0], "a", w);
        add_in(c, c->pos[1], "b", w);
        if (c->cin) { if (check) want_width(c, c->cin, 1, "cin"); add_in(c, c->cin, "cin", 1); }
        add_out(c, "sum", w);
        add_out(c, "cout", 1);
        break;
    case K_COMPARE:
        need_args(c, 2, 2, "two inputs");
        w = argw(c->pos[0]);
        if (argw(c->pos[1]) > w) w = argw(c->pos[1]);
        if (check) { want_width(c, c->pos[0], w, "both inputs"); want_width(c, c->pos[1], w, "both inputs"); }
        add_in(c, c->pos[0], "a", w);
        add_in(c, c->pos[1], "b", w);
        add_out(c, "lt", 1);
        add_out(c, "eq", 1);
        add_out(c, "gt", 1);
        c->defport = -1;
        break;
    case K_DFF:
        need_args(c, 2, 2, "a data input and a clock: dff(d, clk)");
        w = argw(c->pos[0]);
        if (check) want_width(c, c->pos[1], 1, "the clock");
        add_in(c, c->pos[0], "d", w);
        add_in(c, c->pos[1], "clk", 1);
        goto flags;
    case K_TFF:
        need_args(c, 2, 2, "t and a clock: tff(t, clk)");
        w = 1;
        if (check) { want_width(c, c->pos[0], 1, "t"); want_width(c, c->pos[1], 1, "the clock"); }
        add_in(c, c->pos[0], "t", 1);
        add_in(c, c->pos[1], "clk", 1);
        goto flags;
    case K_JKFF:
        need_args(c, 3, 3, "j, k and a clock: jkff(j, k, clk)");
        w = 1;
        for (i = 0; check && i < 3; i++) want_width(c, c->pos[i], 1, i < 2 ? "j and k" : "the clock");
        add_in(c, c->pos[0], "j", 1);
        add_in(c, c->pos[1], "k", 1);
        add_in(c, c->pos[2], "clk", 1);
        goto flags;
    case K_COUNTER:
        need_args(c, 1, 1, "a clock: counter(clk, bits=4)");
        w = c->bits ? c->bits : (c->d && argw(c->d)) ? argw(c->d) : 4;
        c->bits = w;
        if (check) want_width(c, c->pos[0], 1, "the clock");
        add_in(c, c->pos[0], "clk", 1);
    flags:
        if (c->en)  { if (check) want_width(c, c->en, 1, "en");   add_in(c, c->en, "en", 1); }
        if (c->rst) { if (check) want_width(c, c->rst, 1, "rst"); add_in(c, c->rst, "rst", 1); }
        if (c->kind == K_COUNTER && (c->load || c->d)) {
            if (!c->load || !c->d)
                fail(c->call, "a counter that can be loaded needs both load = and d =: counter(clk, load = l, d = x)");
            if (check) { want_width(c, c->load, 1, "load"); want_width(c, c->d, w, "d"); }
            add_in(c, c->load, "load", 1);
            add_in(c, c->d, "d", w);
        }
        if (c->kind == K_COUNTER && c->down) {
            if (check) want_width(c, c->down, 1, "down");
            add_in(c, c->down, "down", 1);
        }
        if (c->kind == K_COUNTER) { add_out(c, "q", w); add_out(c, "carry", 1); }
        else { add_out(c, "q", w); add_out(c, "nq", w); }
        break;
    case K_TRI:
        need_args(c, 2, 2, "a value and an enable: tri(x, en)");
        w = argw(c->pos[0]);
        if (check) want_width(c, c->pos[1], 1, "the enable");
        add_in(c, c->pos[0], "in", w);
        add_in(c, c->pos[1], "en", 1);
        add_out(c, "out", w);
        break;
    case K_ROM:
        need_args(c, 1, 1, "an address: rom(addr, bits = 8, file = \"program.hex\")");
        s = argw(c->pos[0]);
        if (check && (s < 1 || s > 16))
            fail(c->pos[0], "the address of a rom is 1 to 16 bits (up to 65536 words), not %d", s);
        w = c->bits ? c->bits : 8;
        add_in(c, c->pos[0], "addr", s);
        add_out(c, "q", w);
        break;
    case K_RAM:
        need_args(c, 4, 4, "an address, data in, write enable and a clock: ram(addr, din, we, clk)");
        s = argw(c->pos[0]);
        w = argw(c->pos[1]);
        if (check && (s < 1 || s > 16))
            fail(c->pos[0], "the address of a ram is 1 to 16 bits (up to 65536 words), not %d", s);
        if (check && !w) fail(c->pos[1], "the data in of a ram needs a width; use a wire with one, like d[8]");
        if (check) { want_width(c, c->pos[2], 1, "we"); want_width(c, c->pos[3], 1, "the clock"); }
        add_in(c, c->pos[0], "addr", s);
        add_in(c, c->pos[1], "din", w);
        add_in(c, c->pos[2], "we", 1);
        add_in(c, c->pos[3], "clk", 1);
        add_out(c, "q", w);
        break;
    }
    if (check)
        for (i = 0; i < c->nout; i++)
            if (c->outw[i] > MAXW) fail(c->call, "%s would be %d bits wide; the limit is %d",
                                        c->kname, c->outw[i], MAXW);
}

static void port_list(Call *c, char *buf)
{
    int i;
    buf[0] = 0;
    if (c->indexed) { sprintf(buf, "o0 to o%d", c->nout - 1); return; }
    for (i = 0; i < c->nout; i++) {
        strcat(buf, c->outname[i]);
        strcat(buf, i + 2 < c->nout ? ", " : i + 1 < c->nout ? " and " : "");
    }
}

static int find_port(Call *c, const char *p)
{
    int i;
    for (i = 0; i < c->nout; i++) if (!strcmp(c->outname[i], p)) return i;
    return -1;
}

static int is_default_port(gram_node *call, const char *port)
{
    Call c;
    analyze(call, &c, 0);
    return c.defport >= 0 && !strcmp(c.outname[c.defport], port);
}

/* the call a name was assigned, if any */
static gram_node *sym_call(Sym *s)
{
    return s && !s->conn && s->expr && s->expr->rule == WIRE_R_call ? s->expr : NULL;
}

/* ---- widths ------------------------------------------------------------ */

static int sym_width(Sym *s)
{
    int w = 0, i;
    if (s->state == 2) return s->w;
    if (s->state == 1) return 0;        /* a loop: decided by the other inputs */
    s->state = 1;
    if (s->dw) w = s->dw;
    else if (s->dkind == D_INPUT || s->dkind == D_BUTTON || s->dkind == D_CLOCK || s->dkind == D_LED) w = 1;
    else if (s->dkind == D_HEX) w = 4;
    else if (s->dkind == D_KEYPAD) w = 5;
    else if (s->expr) {
        if (sym_call(s)) {
            Call c;
            analyze(s->expr, &c, 0);
            w = c.defport >= 0 ? c.outw[c.defport] : 0;
        } else w = ew(s->expr);
    } else for (i = 0; i < s->nparts; i++) if (s->parts[i].hi + 1 > w) w = s->parts[i].hi + 1;
    if (s->dkind == D_SEG7 && !w) w = 7;
    s->state = 2;
    s->w = w;
    return w;
}

/* width of one port of a named component */
static int port_width(Sym *s, gram_node *call, const char *port)
{
    Call c;
    int p;
    (void)s;
    analyze(call, &c, 0);
    p = find_port(&c, port);
    return p >= 0 ? c.outw[p] : 0;
}

static int ew(gram_node *e)
{
    gram_node *c;
    int w = 0, x;
    switch (e->rule) {
    case WIRE_R_number: literal(e, &w); return w;
    case WIRE_R_ident: { Sym *s = lookup(e); return s ? sym_width(s) : 0; }
    case WIRE_R_notx: return ew(e->child);
    case WIRE_R_orx: case WIRE_R_xorx: case WIRE_R_andx:
        for (c = e->child; c; c = c->next) if ((x = ew(c)) > w) w = x;
        return w;
    case WIRE_R_call: {
        Call cl;
        analyze(e, &cl, 0);
        return cl.defport >= 0 ? cl.outw[cl.defport] : 0;
    }
    case WIRE_R_concat:
        for (c = e->child; c; c = c->next) w += ew(c);
        return w;
    case WIRE_R_post: {
        gram_node *base = e->child, *call = NULL;
        Sym *s = NULL;
        c = base->next;
        if (base->rule == WIRE_R_call) call = base;
        if (base->rule == WIRE_R_ident) { s = lookup(base); call = sym_call(s); }
        if (call && c) {
            Call cl;
            analyze(call, &cl, 0);
            if (c->rule == WIRE_R_port) {
                char p[NAMEW];
                snprintf(p, sizeof p, "%.*s", TEXT(c->child));
                w = port_width(s, call, p);
                c = c->next;
            } else if (cl.indexed) {
                w = cl.outw[0];
                c = c->next;
            } else w = cl.defport >= 0 ? cl.outw[cl.defport] : 0;
        } else w = ew(base);
        for (; c; c = c->next) {
            if (c->rule != WIRE_R_range) return 0;
            w = (c->child->next ? number(c->child) - number(c->child->next) : 0) + 1;
        }
        return w;
    }
    }
    return 0;
}

/* ---- building ---------------------------------------------------------- */

typedef struct { int src, hi, lo, out, inst; const gram_node *at; } Tap;
static Tap *taps; static int ntaps, captaps;

static void join(int a, int b)
{
    a = find(a);
    b = find(b);
    if (a == b) return;
    if (nets[a].w && nets[b].w && nets[a].w != nets[b].w)
        fail(nets[b].at, "a %d-bit and a %d-bit wire are connected", nets[a].w, nets[b].w);
    if (!nets[a].w) nets[a].w = nets[b].w;
    if (!nets[a].name) { nets[a].name = nets[b].name; nets[a].nlen = nets[b].nlen; }
    nets[b].parent = a;
}

static void set_width(int n, int w, const gram_node *at)
{
    n = find(n);
    if (nets[n].w && w && nets[n].w != w) {
        if (nets[n].name)
            fail(at, "%.*s is %d bit%s, but this is %d bit%s",
                 nets[n].nlen, nets[n].name, nets[n].w, plural(nets[n].w), w, plural(w));
        fail(at, "this is %d bit%s, but %d bit%s are needed here", w, plural(w), nets[n].w, plural(nets[n].w));
    }
    if (!nets[n].w) nets[n].w = w;
}

static int out_net(int dst, int w, const gram_node *at)
{
    int n = dst >= 0 ? dst : new_net(0);
    set_width(n, w, at);
    if (!nets[find(n)].at) nets[find(n)].at = at;
    return n;
}

static int build(gram_node *e, int dst, int want);

static void name_node(Node *n, Sym *s)
{
    if (s) { n->name = s->name; n->nlen = s->len; }
}

static int build_const(gram_node *e, int dst, int want)
{
    int fixed, w;
    unsigned long v = literal(e, &fixed);
    Node *n;
    w = fixed ? fixed : want ? want : bits_for(v);
    if (want && fixed && fixed != want)
        fail(e, "%.*s is %d bits, but %d bit%s are needed here", TEXT(e), fixed, want, plural(want));
    if (w < MAXW && (v >> w))
        fail(e, "%.*s does not fit in %d bit%s", TEXT(e), w, plural(w));
    n = new_node(K_CONST, e);
    n->value = v;
    node_out(n, "out", out_net(dst, w, e));
    return n->out[0];
}

/* ---- memory contents ---------------------------------------------------- */

/* Contents of a rom or ram, from `data = "..."` or a file named by
 * `file = "..."`. The text is numbers in hex (3E, or 0x3E; 0b0011 is
 * binary), separated by spaces, commas or new lines, filling words from
 * address 0 up. `@100` moves to address 0x100. `#`, `;` and `//` start
 * comments. */
static const char *mem_where; static gram_node *mem_at;
static void mem_fail(int line, const char *f, ...)
{
    va_list ap;
    char msg[400];
    va_start(ap, f);
    vsnprintf(msg, sizeof msg, f, ap);
    va_end(ap);
    if (!mem_where) fail(mem_at, "in the data, %s", msg);
    fprintf(stderr, "%s:%d: error: %s\n", mem_where, line, msg);
    exit(1);
}

static void parse_mem(Node *n, const char *t, size_t len)
{
    long size = 1L << n->memabits, addr = 0;
    size_t i = 0;
    int line = 1;
    unsigned long max = n->membits >= 32 ? 0xFFFFFFFFUL : (1UL << n->membits) - 1;
    while (i < len) {
        char c = t[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == ',') { i++; continue; }
        if (c == '#' || c == ';' || (c == '/' && i + 1 < len && t[i + 1] == '/')) {
            while (i < len && t[i] != '\n') i++;
            continue;
        }
        {
            int at = c == '@', base = 16, digits = 0;
            unsigned long v = 0;
            size_t start;
            if (at) i++;
            if (i + 1 < len && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) i += 2;
            else if (i + 2 < len && t[i] == '0' && (t[i + 1] == 'b' || t[i + 1] == 'B') &&
                     (t[i + 2] == '0' || t[i + 2] == '1')) { base = 2; i += 2; }
            start = i;
            while (i < len) {
                char d = t[i];
                int x = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10
                      : d >= 'A' && d <= 'F' ? d - 'A' + 10 : d == '_' ? -2 : -1;
                if (x == -2) { i++; continue; }
                if (x < 0 || x >= base) break;
                if (v > (0xFFFFFFFFUL - (unsigned long)x) / (unsigned long)base) mem_fail(line, "a number is too big");
                v = v * (unsigned long)base + (unsigned long)x;
                digits++;
                i++;
            }
            if (!digits || (i < len && t[i] != ' ' && t[i] != '\t' && t[i] != '\r' && t[i] != '\n' &&
                            t[i] != ',' && t[i] != '#' && t[i] != ';' && t[i] != '/')) {
                size_t e = start;
                while (e < len && e < start + 20 && t[e] != ' ' && t[e] != '\n' && t[e] != ',') e++;
                mem_fail(line, "%s%.*s is not a hex number", at ? "@" : "", (int)(e - start), t + start);
            }
            if (at) {
                if ((long)v >= size) mem_fail(line, "@%lX is past the end (the %s has %ld words)", v, kind_names[n->kind], size);
                addr = (long)v;
                continue;
            }
            if (addr >= size) mem_fail(line, "more data than the %s holds (%ld words)", kind_names[n->kind], size);
            if (v > max) mem_fail(line, "%lX does not fit in %d bit%s", v, n->membits, plural(n->membits));
            n->mem[addr++] = v;
            if (addr > n->memlen) n->memlen = addr;
        }
    }
}

static char *read_file(const char *path, size_t *len);

static void load_mem(int k, gram_node *setting)
{
    Node *n = &nodes[k];
    gram_node *key, *v;
    n->mem = (unsigned long *)calloc((size_t)1 << n->memabits, sizeof *n->mem);
    if (!n->mem) { fprintf(stderr, "out of memory\n"); exit(1); }
    n->memlen = 0;
    if (!setting) return;
    key = setting->child;
    v = key->next;
    mem_at = v;
    if (same(key, "file", 4)) {
        const char *src = files[file_of(v)].name;
        int dl = 0, i;
        char *path, *text;
        size_t len, nl = v->len - 2;
        for (i = 0; src[i]; i++) if (src[i] == '/' || src[i] == '\\') dl = i + 1;
        if (nl && (v->text[1] == '/' || v->text[1] == '\\' || (nl > 1 && v->text[2] == ':'))) dl = 0;
        path = (char *)malloc((size_t)dl + nl + 1);
        memcpy(path, src, (size_t)dl);
        memcpy(path + dl, v->text + 1, nl);
        path[dl + nl] = 0;
        text = read_file(path, &len);
        if (!text) fail(v, "cannot read %s", path);
        mem_where = path;
        parse_mem(n, text, len);
        free(text);
        mem_where = NULL;
    } else {
        mem_where = NULL;
        parse_mem(n, v->text + 1, v->len - 2);
    }
}

/* build a call; the output in `dst` is the default port, other ports of a
 * named component (`s`) use the nets made for `s.port` references */
static Node *instantiate(Call *c, int dst, Sym *s);

static Node *build_call(gram_node *call, int dst, Sym *s)
{
    Call c;
    Node *n;
    int i, k;
    analyze(call, &c, 1);
    if (c.kind == K_USER) return instantiate(&c, dst, s);
    k = nnodes;
    new_node(c.kind, call);
    for (i = 0; i < c.nin; i++) {
        int net = build(c.in[i], -1, c.inw[i]);
        n = &nodes[k];
        set_width(net, c.inw[i], c.in[i]);
        node_in(n, c.inname[i], net);
    }
    n = &nodes[k];
    name_node(n, s);
    n->bits = c.bits ? c.bits : 4;
    if (c.kind == K_ROM || c.kind == K_RAM) {
        n->membits = c.outw[0];
        n->memabits = c.inw[0];
        load_mem(k, c.data);
    }
    for (i = 0; i < c.nout; i++) {
        int net;
        if (i == c.defport && dst >= 0) net = dst;
        else if (s) net = port_net(s, c.outname[i], call);
        else net = new_net(0);
        node_out(&nodes[k], c.outname[i], out_net(net, c.outw[i], call));
    }
    return &nodes[k];
}

static int gate_kind(int rule)
{
    return rule == WIRE_R_orx ? K_OR : rule == WIRE_R_xorx ? K_XOR : K_AND;
}

static int tap(int src, int hi, int lo, int dst, const gram_node *at)
{
    int w = nets[find(src)].w;
    if (hi < lo) fail(at, "write the high bit first: [%d:%d]", lo, hi);
    if (w && hi >= w) fail(at, "bit %d is out of range; this is a %d-bit value (bits %d..0)", hi, w, w - 1);
    taps = (Tap *)grow(taps, &captaps, ntaps + 1, sizeof *taps);
    taps[ntaps].src = src;
    taps[ntaps].hi = hi;
    taps[ntaps].lo = lo;
    taps[ntaps].out = out_net(dst, hi - lo + 1, at);
    taps[ntaps].at = at;
    taps[ntaps].inst = cur_inst;
    return taps[ntaps++].out;
}

static int build_post(gram_node *e, int dst)
{
    gram_node *base = e->child, *c = base->next, *call = NULL;
    Sym *s = NULL;
    int net = -1;
    if (base->rule == WIRE_R_ident) {
        s = lookup(base);
        if (!s) fail(base, "%.*s is not defined", TEXT(base));
        call = sym_call(s);
    } else if (base->rule == WIRE_R_call) call = base;

    if (c->rule == WIRE_R_port) {
        char p[NAMEW], buf[400];
        Call cl;
        if (!call) fail(c, "%.*s is not a component, so it has no .%.*s",
                        TEXT(base), TEXT(c->child));
        analyze(call, &cl, 0);
        snprintf(p, sizeof p, "%.*s", TEXT(c->child));
        if (c->child->len >= sizeof p || find_port(&cl, p) < 0) {
            port_list(&cl, buf);
            fail(c->child, "%s has no output called %.*s; it has %s",
                 cl.kname, TEXT(c->child), buf);
        }
        if (s) net = port_net(s, p, c);
        else {
            Node *n = build_call(call, -1, NULL);
            net = n->out[find_port(&cl, p)];
        }
        c = c->next;
    } else if (call) {
        Call cl;
        analyze(call, &cl, 0);
        if (cl.indexed) {   /* dm[2] is output o2 of a demux */
            char p[NAMEW];
            int i = number(c->child);
            if (c->child->next) fail(c, "pick one output of %s: [i], not a range", cl.kname);
            if (i >= cl.nout) fail(c, "%s has outputs 0 to %d", cl.kname, cl.nout - 1);
            sprintf(p, "o%d", i);
            if (s) net = port_net(s, p, c);
            else net = build_call(call, -1, NULL)->out[i];
            c = c->next;
        }
    }
    if (net < 0) net = build(base, -1, 0);
    for (; c; c = c->next) {
        int hi, lo;
        if (c->rule == WIRE_R_port) fail(c, "only components have .outputs");
        hi = number(c->child);
        lo = c->child->next ? number(c->child->next) : hi;
        net = tap(net, hi, lo, c->next ? -1 : dst, c);
        if (!c->next && dst >= 0) return net;
    }
    if (dst >= 0) { join(dst, net); return dst; }
    return net;
}

static int build(gram_node *e, int dst, int want)
{
    gram_node *c;
    Node *n;
    int k, w = 0, i;
    switch (e->rule) {
    case WIRE_R_number:
        return build_const(e, dst, want);
    case WIRE_R_ident: {
        Sym *s = lookup(e);
        gram_node *call;
        if (!s) fail(e, "%.*s is not defined", TEXT(e));
        call = sym_call(s);
        if (call) {
            Call cl;
            char buf[400];
            analyze(call, &cl, 0);
            if (cl.defport < 0) {
                port_list(&cl, buf);
                fail(e, "%.*s is a %s with several outputs; pick one: %s",
                     TEXT(e), cl.kname, buf);
            }
        }
        if (dst >= 0) { join(dst, s->net); return dst; }
        return s->net;
    }
    case WIRE_R_notx:
        w = is_const(e->child) ? 0 : ew(e->child);   /* ~0 takes the width it is used at */
        if (!w) w = want;
        k = nnodes;
        new_node(K_NOT, e);
        i = build(e->child, -1, w);
        nodes[k].nin = 0;
        node_in(&nodes[k], "in", i);
        w = nets[find(i)].w;
        node_out(&nodes[k], "out", out_net(dst, w, e));
        return nodes[k].out[0];
    case WIRE_R_orx: case WIRE_R_xorx: case WIRE_R_andx:
        for (c = e->child; c; c = c->next) {
            int x = is_const(c) ? 0 : ew(c);
            if (x > w) w = x;
        }
        if (!w) w = want ? want : 1;
        k = nnodes;
        new_node(gate_kind(e->rule), e);
        for (c = e->child; c; c = c->next) {
            int x = is_const(c) ? 0 : ew(c);
            if (x && x != w)
                fail(c, "%.*s is %d bit%s, but the other side is %d bit%s",
                     TEXT(c), x, plural(x), w, plural(w));
            if (nodes[k].nin >= 16) fail(c, "more than 16 inputs to one gate; split it up");
            i = build(c, -1, w);
            node_in(&nodes[k], "", i);
        }
        node_out(&nodes[k], "out", out_net(dst, w, e));
        return nodes[k].out[0];
    case WIRE_R_call: {
        Call cl;
        char buf[400];
        analyze(e, &cl, 0);
        if (cl.defport < 0) {
            port_list(&cl, buf);
            fail(e, "%s has several outputs; pick one with .%s (outputs: %s)",
                 cl.kname, cl.outname[0], buf);
        }
        n = build_call(e, dst, NULL);
        return n->out[cl.defport];
    }
    case WIRE_R_post:
        return build_post(e, dst);
    case WIRE_R_concat: {
        int parts[MAXIO], widths[MAXIO], np = 0, total = 0, sum;
        for (c = e->child; c; c = c->next) {
            if (is_const(c))
                fail(c, "give constants in {...} a width: write 0b0, 0b0000 or 0x0");
            if (np >= MAXIO) fail(c, "too many parts");
            parts[np] = build(c, -1, 0);
            widths[np] = nets[find(parts[np])].w;
            total += widths[np++];
        }
        if (total > MAXW) fail(e, "this is %d bits; the limit is %d", total, MAXW);
        sum = total;
        k = nnodes;
        n = new_node(K_MERGE, e);
        for (i = 0; i < np; i++) {      /* first part is the most significant */
            total -= widths[i];
            n->range[n->nin][0] = total + widths[i] - 1;
            n->range[n->nin][1] = total;
            node_in(n, "", parts[i]);
        }
        node_out(&nodes[k], "out", out_net(dst, sum, e));
        return nodes[k].out[0];
    }
    }
    fail(e, "cannot use %s here", e->tag);
    return -1;
}

/* ---- statements -------------------------------------------------------- */

static const char *title; static int titlelen;

static void declare(gram_node *d)
{
    gram_node *k = d->child, *it;
    int dk;
    for (dk = 1; dk <= D_KEYPAD; dk++)
        if (same(k, dkind_names[dk], (int)strlen(dkind_names[dk]))) break;
    for (it = k->next; it; it = it->next) {
        Sym *s;
        gram_node *id = it->child, *r = NULL, *opts = NULL, *x, *o;
        for (x = id->next; x; x = x->next) {
            if (x->rule == WIRE_R_range) r = x;
            if (x->rule == WIRE_R_opts) opts = x;
        }
        s = lookup(id);
        if (s && s->decl) fail(id, "%.*s is already declared on line %d", TEXT(id), line_of(s->decl));
        s = intern(id);
        s->dkind = dk;
        s->decl = id;
        s->opts = opts;
        if (r) {
            if (r->child->next) fail(r, "write a width as [n], e.g. %.*s[8]", TEXT(id));
            s->dw = number(r->child);
            if (s->dw < 1 || s->dw > MAXW) fail(r, "widths go from 1 to %d", MAXW);
            if (dk == D_LED || dk == D_BUTTON || dk == D_CLOCK)
                if (s->dw != 1) fail(r, "a %s is 1 bit", dkind_names[dk]);
            if (dk == D_SEG7 && s->dw != 7 && s->dw != 8)
                fail(r, "a seg7 is 7 bits (segments a..g) or 8 (with the dot)");
            if (dk == D_HEX && s->dw > 4) fail(r, "a hex display shows 4 bits");
            if (dk == D_KEYPAD) fail(r, "a keypad is always 5 bits: k[3:0] is the key, k[4] is 1 while it is held");
        }
        for (o = opts ? opts->child : NULL; o; o = o->next) {
            gram_node *key = o->child;
            if (dk == D_LED && same(key, "color", 5)) continue;
            if (dk == D_CLOCK && same(key, "hz", 2)) continue;
            fail(key, "%s has no setting called %.*s%s", dkind_names[dk], TEXT(key),
                 dk == D_LED ? " (it has color)" : dk == D_CLOCK ? " (it has hz)" : "");
        }
    }
}

static void assign_to(gram_node *target, gram_node *expr, gram_node *at, int pre)
{
    gram_node *id = target->child, *r = id->next;
    Sym *s = intern(id);
    if (s->dkind == D_INPUT || s->dkind == D_BUTTON || s->dkind == D_CLOCK || s->dkind == D_KEYPAD)
        fail(id, "%.*s is an %s; it gets its value from the page, not from the circuit",
             TEXT(id), dkind_names[s->dkind]);
    if (s->dkind == D_PIN_IN)
        fail(id, "%.*s is an input of %s; its value comes from where %s is used",
             TEXT(id), comps[sc->comp].name, comps[sc->comp].name);
    if (!r) {
        if (s->expr && s->dkind == D_BUS) return;     /* one more thing driving a bus */
        if (s->expr) {
            int tri = expr->rule == WIRE_R_call && same(expr->child, "tri", 3);
            fail(id, "%.*s is already given a value on line %d%s%.*s", TEXT(id), line_of(s->at),
                 tri ? ". For several tri() outputs to share a wire, declare it as a bus: bus " : "",
                 tri ? (int)id->len : 0, id->text);
        }
        if (s->nparts)
            fail(id, "%.*s already has single bits assigned on line %d", TEXT(id), line_of(s->parts[0].at));
        s->expr = expr;
        s->at = at;
        s->conn = pre == -2 || s->dkind == D_BUS;   /* a bus is a wire, not a part's name */
        if (!s->decl) s->decl = id;
    } else {
        Part *p;
        if (s->expr)
            fail(id, "%.*s is already given a value on line %d", TEXT(id), line_of(s->at));
        s->parts = (Part *)grow(s->parts, &s->capparts, s->nparts + 1, sizeof *s->parts);
        p = &s->parts[s->nparts++];
        p->hi = number(r->child);
        p->lo = r->child->next ? number(r->child->next) : p->hi;
        if (p->hi < p->lo) fail(r, "write the high bit first: [%d:%d]", p->lo, p->hi);
        p->expr = expr;
        p->at = at;
        p->net = pre;
        if (!s->decl) s->decl = id;
    }
}

/* ---- output ------------------------------------------------------------ */

static FILE *jf;

static void jstr(const char *s, int n)
{
    int i;
    fputc('"', jf);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') fprintf(jf, "\\%c", c);
        else if (c == '\n') fputs("\\n", jf);
        else if (c == '\r') ;
        else if (c == '\t') fputs("\\t", jf);
        else if (c < 32) fprintf(jf, "\\u%04x", c);
        else if (c == '<' ) fputs("\\u003c", jf);   /* keeps </script> out */
        else fputc(c, jf);
    }
    fputc('"', jf);
}

static void jcstr(const char *s) { jstr(s, (int)strlen(s)); }

static int *canon; /* net -> index in output, -1 if unused */

static void write_json(void)
{
    int i, j, k = 0;
    fputs("{\"title\":", jf);
    if (title) jstr(title + 1, titlelen - 2); else jcstr("");
    fputs(",\"file\":", jf);
    jcstr(src_name);
    fputs(",\"source\":", jf);
    jstr(files[0].text, (int)files[0].len);
    fputs(",\"files\":[", jf);
    for (i = 0; i < nfiles; i++) {
        if (i) fputc(',', jf);
        fputs("{\"name\":", jf);
        jcstr(files[i].name);
        fputs(",\"source\":", jf);
        jstr(files[i].text, (int)files[i].len);
        fputc('}', jf);
    }
    fputs("],\"insts\":[", jf);
    for (i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (i) fputs(",\n", jf);
        fputs("{\"comp\":", jf);
        jcstr(in->comp >= 0 ? comps[in->comp].name : "");
        fputs(",\"name\":", jf);
        if (in->name) jstr(in->name, in->nlen); else jcstr("");
        fprintf(jf, ",\"parent\":%d,\"line\":%d,\"file\":%d", in->parent, in->line, in->file);
        if (in->comp >= 0)
            fprintf(jf, ",\"defFile\":%d,\"defLine\":%d", file_of(comps[in->comp].node), line_of(comps[in->comp].node));
        fputc('}', jf);
    }
    fputc(']', jf);
    fputs(",\"nets\":[", jf);
    for (i = 0; i < nnets; i++) {
        if (canon[i] < 0) continue;
        if (k++) fputc(',', jf);
        fprintf(jf, "{\"w\":%d,\"name\":", nets[i].w);
        if (nets[i].name) jstr(nets[i].name, nets[i].nlen); else jcstr("");
        fputc('}', jf);
    }
    fputs("],\"nodes\":[", jf);
    for (i = 0, k = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->dead) continue;
        if (k++) fputs(",\n", jf);
        fputs("{\"kind\":", jf);
        jcstr(kind_names[n->kind]);
        fputs(",\"name\":", jf);
        if (n->name) jstr(n->name, n->nlen); else jcstr("");
        fprintf(jf, ",\"line\":%d,\"file\":%d,\"inst\":%d,\"in\":[", n->line, n->file, n->inst);
        for (j = 0; j < n->nin; j++) fprintf(jf, "%s%d", j ? "," : "", canon[find(n->in[j])]);
        fputs("],\"inNames\":[", jf);
        for (j = 0; j < n->nin; j++) { if (j) fputc(',', jf); jcstr(n->inname[j]); }
        fputs("],\"out\":[", jf);
        for (j = 0; j < n->nout; j++) fprintf(jf, "%s%d", j ? "," : "", canon[find(n->out[j])]);
        fputs("],\"outNames\":[", jf);
        for (j = 0; j < n->nout; j++) { if (j) fputc(',', jf); jcstr(n->outname[j]); }
        fputc(']', jf);
        if (n->kind == K_SPLIT || n->kind == K_MERGE) {
            int m = n->kind == K_SPLIT ? n->nout : n->nin;
            fputs(",\"ranges\":[", jf);
            for (j = 0; j < m; j++) fprintf(jf, "%s[%d,%d]", j ? "," : "", n->range[j][0], n->range[j][1]);
            fputc(']', jf);
        }
        if (n->kind == K_CONST) fprintf(jf, ",\"value\":%lu", n->value);
        if (n->kind == K_COUNTER) fprintf(jf, ",\"bits\":%d", n->bits);
        if (n->kind == K_ROM || n->kind == K_RAM) {
            long a = 0, e, last, m;
            int seg = 0;
            fprintf(jf, ",\"bits\":%d,\"abits\":%d,\"init\":[", n->membits, n->memabits);
            /* runs of contents, as [start, [words]]; long runs of zeros are left out */
            while (a < n->memlen) {
                if (!n->mem[a]) { a++; continue; }
                for (last = e = a; e < n->memlen && e - last <= 16; e++) if (n->mem[e]) last = e;
                fprintf(jf, "%s[%ld,[", seg++ ? "," : "", a);
                for (m = a; m <= last; m++) fprintf(jf, "%s%lu", m > a ? "," : "", n->mem[m]);
                fputs("]]", jf);
                a = last + 1;
            }
            fputc(']', jf);
        }
        if (n->kind == K_CLOCK) fprintf(jf, ",\"hz\":%ld.%02ld", n->hz / 100, n->hz % 100);
        if (n->opt) { fputs(",\"color\":", jf); jstr(n->opt, n->optlen); }
        fputc('}', jf);
    }
    fputs("]}", jf);
}

static int write_html(const char *path)
{
    const char *mark = "/*WIRE_DATA*/";
    const char *at = strstr(wire_view, mark);
    jf = fopen(path, "wb");
    if (!jf) return 0;
    if (!at) at = wire_view + strlen(wire_view);
    fwrite(wire_view, 1, (size_t)(at - wire_view), jf);
    write_json();
    if (*at) fputs(at + strlen(mark), jf);
    return fclose(jf) == 0;
}

/* ---- main -------------------------------------------------------------- */

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void put(int c, void *ctx) { fputc(c, (FILE *)ctx); }

static long opt_hz(gram_node *opts)
{
    gram_node *o;
    long hz = 100;
    for (o = opts ? opts->child : NULL; o; o = o->next) {
        gram_node *v = o->child->next;
        size_t i;
        long whole = 0;
        if (v->rule != WIRE_R_number) fail(v, "hz is a number, like 2 or 0x4");
        for (i = 0; i < v->len; i++) {
            if (v->text[i] < '0' || v->text[i] > '9') fail(v, "write hz in decimal");
            whole = whole * 10 + (v->text[i] - '0');
            if (whole > 1000) fail(v, "a clock runs at most at 1000 hz");
        }
        if (whole < 1) fail(v, "hz must be at least 1");
        hz = whole * 100;
    }
    return hz;
}

static void opt_color(Node *n, gram_node *opts)
{
    gram_node *o;
    for (o = opts ? opts->child : NULL; o; o = o->next) {
        gram_node *v = o->child->next;
        if (v->rule == WIRE_R_string) { n->opt = v->text + 1; n->optlen = (int)v->len - 2; }
        else { n->opt = v->text; n->optlen = (int)v->len; }
    }
}

/* ---- instances --------------------------------------------------------- */

static int expanding[64], depth;
static int n_in, n_out;

static void compile_scope(gram_node *first, int top);

/* Expand component `ci` once, its inputs coming from the nets `outer`.
 * Returns the new instance; inner_out gets the nets that hold its outputs
 * inside it. Pin nodes mark the boundary, so the page can draw it as a chip. */
static int expand(int ci, const int *outer, int *inner_out, const gram_node *at, Sym *named)
{
    Comp *cp = &comps[ci];
    Scope scope, *saved = sc;
    int id, saved_inst = cur_inst, i;
    for (i = 0; i < depth; i++)
        if (expanding[i] == ci) fail(at, "%s uses itself, so it would never end", cp->name);
    if (depth >= 64) fail(at, "components are nested more than 64 deep");
    insts = (Inst *)grow(insts, &capinsts, ninsts + 1, sizeof *insts);
    id = ninsts++;
    insts[id].comp = ci;
    insts[id].parent = cur_inst;
    insts[id].name = named ? named->name : NULL;
    insts[id].nlen = named ? named->len : 0;
    insts[id].line = line_of(at);
    insts[id].file = file_of(at);
    expanding[depth++] = ci;
    cur_inst = id;
    memset(&scope, 0, sizeof scope);
    scope.comp = ci;
    sc = &scope;
    for (i = 0; i < cp->nin + cp->nout; i++) {
        int isin = i < cp->nin, k = isin ? i : i - cp->nin;
        gram_node *pid = isin ? cp->inid[k] : cp->outid[k];
        Sym *ps = intern(pid);
        ps->dkind = isin ? D_PIN_IN : D_PIN_OUT;
        ps->dw = isin ? cp->inw[k] : cp->outw[k];
        ps->decl = pid;
        ps->net = new_net(ps->dw);
        nets[ps->net].name = ps->name;
        nets[ps->net].nlen = ps->len;
        nets[ps->net].at = pid;
        if (isin) {
            Node *n = new_node(K_PIN_IN, pid);
            name_node(n, ps);
            node_in(n, "", outer[k]);
            node_out(n, "", ps->net);
        }
    }
    compile_scope(cp->body->child, 0);
    for (i = 0; i < cp->nout; i++) {
        Sym *ps = lookup(cp->outid[i]);
        if (!ps->expr && !ps->nparts)
            fail(cp->outid[i], "output %s of %s is never given a value", cp->outname[i], cp->name);
        inner_out[i] = ps->net;
    }
    sc = saved;
    depth--;
    cur_inst = saved_inst;
    return id;
}

/* a call of a component: build its inputs here, expand it, and connect
 * its outputs through pins to this scope */
static Node uresult;
static Node *instantiate(Call *c, int dst, Sym *s)
{
    Comp *cp = &comps[c->comp];
    int outer[MAXIO], inner[MAXIO], i, id, saved;
    for (i = 0; i < c->nin; i++) {
        outer[i] = build(c->in[i], -1, c->inw[i]);
        set_width(outer[i], c->inw[i], c->in[i]);
    }
    id = expand(c->comp, outer, inner, c->call, s);
    cp->used = 1;
    memset(&uresult, 0, sizeof uresult);
    saved = cur_inst;
    cur_inst = id;
    for (i = 0; i < c->nout; i++) {
        int net;
        Node *n;
        if (i == c->defport && dst >= 0) net = dst;
        else if (s) net = port_net(s, c->outname[i], c->call);
        else net = new_net(0);
        net = out_net(net, c->outw[i], c->call);
        n = new_node(K_PIN_OUT, cp->outid[i]);
        n->name = cp->outname[i];
        n->nlen = (int)strlen(cp->outname[i]);
        node_in(n, "", inner[i]);
        node_out(n, "", net);
        uresult.out[uresult.nout++] = net;
    }
    cur_inst = saved;
    return &uresult;
}

/* one scope: the circuit (top) or the body of a component */
static void compile_scope(gram_node *first, int top)
{
    gram_node *st;
    int i, j;

    /* 1. names */
    for (st = first; st; st = st->next) {
        if (st->rule == WIRE_R_title) {
            if (title) fail(st, "the circuit already has a name");
            title = st->child->text; titlelen = (int)st->child->len;
        } else if (st->rule == WIRE_R_decl) {
            if (!top && !same(st->child, "wire", 4) && !same(st->child, "bus", 3))
                fail(st, "inside a component, declare only wires and buses; its inputs and outputs "
                         "go in the header: component %s(a, b) -> (y) { ... }", comps[sc->comp].name);
            declare(st);
        }
    }
    for (st = first; st; st = st->next) {
        if (st->rule == WIRE_R_assign) assign_to(st->child, st->child->next, st, -1);
        else if (st->rule == WIRE_R_connect) {
            gram_node *t;
            for (t = st->child->next; t; t = t->next) assign_to(t, st->child, st, -2); /* built later */
        }
    }
    for (i = 0; i < sc->nsyms; i++) {
        if (sc->syms[i].net >= 0) continue;        /* ports already have one */
        sc->syms[i].net = new_net(0);
        nets[sc->syms[i].net].name = sc->syms[i].name;
        nets[sc->syms[i].net].nlen = sc->syms[i].len;
        nets[sc->syms[i].net].at = sc->syms[i].decl;
    }

    /* 2. widths */
    for (i = 0; i < sc->nsyms; i++) sym_width(&sc->syms[i]);
    for (i = 0; i < sc->nsyms; i++) {
        Sym *s = &sc->syms[i];
        if (s->dkind == D_SEG7 && s->w != 7 && s->w != 8)
            fail(s->decl, "a seg7 needs 7 bits (segments a..g) or 8 (with the dot), not %d", s->w);
        if (s->dkind == D_HEX && s->w > 4) fail(s->decl, "a hex display shows 4 bits, not %d", s->w);
        if (s->w > MAXW) fail(s->decl, "%.*s is %d bits; the limit is %d", NAME(s), s->w, MAXW);
        if (!sym_call(s) || s->w) nets[s->net].w = s->w;
    }

    /* sources: the page drives these */
    for (i = 0; i < sc->nsyms; i++) {
        Sym *s = &sc->syms[i];
        Node *n;
        int kind = s->dkind == D_INPUT ? K_INPUT : s->dkind == D_BUTTON ? K_BUTTON
                 : s->dkind == D_CLOCK ? K_CLOCK : s->dkind == D_KEYPAD ? K_KEYPAD : -1;
        if (kind < 0) continue;
        n = new_node(kind, s->decl);
        name_node(n, s);
        if (kind == K_CLOCK) n->hz = opt_hz(s->opts);
        node_out(n, "out", s->net);
        n_in++;
    }

    /* 3. build, in the order written */
    for (st = first; st; st = st->next) {
        if (st->rule == WIRE_R_assign) {
            gram_node *t = st->child, *e = t->next;
            Sym *s = lookup(t->child);
            if (!t->child->next) {
                int ew_ = is_const(e) ? 0 : ew(e);
                if (s->w && ew_ && s->w != ew_ && !sym_call(s)) {
                    if (s->dkind == D_PIN_OUT)
                        fail(e, "output %.*s of %s is %d bit%s, but this is %d bit%s", NAME(s),
                             comps[sc->comp].name, s->w, plural(s->w), ew_, plural(ew_));
                    fail(e, "%.*s is %d bit%s, but this is %d bit%s", NAME(s), s->w, plural(s->w), ew_, plural(ew_));
                }
                if (s->dw && ew_ && s->dw != ew_ && sym_call(s)) {
                    Call cl;
                    analyze(e, &cl, 0);
                    fail(e, "%.*s is %d bit%s, but %s gives %d (its output %s)", NAME(s), s->dw, plural(s->dw),
                         cl.kname, ew_, cl.outname[cl.defport]);
                }
                if (e->rule == WIRE_R_call) {
                    Call cl;
                    analyze(e, &cl, 0);
                    if (cl.defport < 0 && s->dkind != D_NONE) build(e, s->net, s->w);  /* says: pick one */
                    build_call(e, cl.defport >= 0 ? s->net : -1, s->dkind == D_BUS ? NULL : s);
                } else build(e, s->net, s->w);
            } else {
                for (j = 0; j < s->nparts; j++)
                    if (s->parts[j].at == st) s->parts[j].net = build(e, -1, s->parts[j].hi - s->parts[j].lo + 1);
            }
        } else if (st->rule == WIRE_R_connect) {
            gram_node *t;
            int net = build(st->child, -1, 0);
            for (t = st->child->next; t; t = t->next) {
                Sym *s = lookup(t->child);
                if (!t->child->next) {
                    if (s->w && nets[find(net)].w && s->w != nets[find(net)].w)
                        fail(t, "%.*s is %d bit%s, but %.*s is %d bit%s", NAME(s), s->w, plural(s->w),
                             TEXT(st->child), nets[find(net)].w, plural(nets[find(net)].w));
                    join(s->net, net);
                } else for (j = 0; j < s->nparts; j++)
                    if (s->parts[j].at == st && s->parts[j].net < 0) s->parts[j].net = net;
            }
        }
    }

    /* single-bit assignments: y[0] = ..., y[3:1] = ... -> one merge */
    for (i = 0; i < sc->nsyms; i++) {
        Sym *s = &sc->syms[i];
        Node *n;
        int w = s->w, b, order[MAXIO];
        unsigned long have = 0;
        if (!s->nparts) continue;
        for (j = 0; j < s->nparts; j++) {
            Part *p = &s->parts[j];
            unsigned long m;
            if (p->hi >= w) fail(p->at, "%.*s is %d bit%s; bit %d is out of range", NAME(s), w, plural(w), p->hi);
            set_width(p->net, p->hi - p->lo + 1, p->expr);
            m = ((p->hi - p->lo + 1 >= 32) ? 0xFFFFFFFFUL : ((1UL << (p->hi - p->lo + 1)) - 1)) << p->lo;
            if (have & m) fail(p->at, "bit %d of %.*s is given a value twice", p->lo, NAME(s));
            have |= m;
        }
        for (b = 0; b < w; b++)
            if (!(have >> b & 1)) fail(s->decl, "bit %d of %.*s is never given a value", b, NAME(s));
        for (j = 0; j < s->nparts; j++) order[j] = j;
        for (j = 1; j < s->nparts; j++) {    /* most significant first */
            int x = order[j], m = j;
            while (m > 0 && s->parts[order[m - 1]].lo < s->parts[x].lo) { order[m] = order[m - 1]; m--; }
            order[m] = x;
        }
        n = new_node(K_MERGE, s->parts[0].at);
        for (j = 0; j < s->nparts; j++) {
            Part *p = &s->parts[order[j]];
            n->range[n->nin][0] = p->hi;
            n->range[n->nin][1] = p->lo;
            node_in(n, "", p->net);
        }
        node_out(n, "out", s->net);
    }

    /* ports of named components that were used but do not exist */
    for (i = 0; i < sc->nprefs; i++) {
        Sym *ps = (Sym *)sc->prefs[i].sym;
        gram_node *call = sym_call(ps);
        Call cl;
        char buf[400];
        if (!call) fail(sc->prefs[i].at, "%.*s is not a component", NAME(ps));
        analyze(call, &cl, 0);
        if (find_port(&cl, sc->prefs[i].port) < 0) {
            port_list(&cl, buf);
            fail(sc->prefs[i].at, "%s has no output called %s; it has %s", cl.kname, sc->prefs[i].port, buf);
        }
    }

    /* sinks: the page shows these */
    for (i = 0; i < sc->nsyms; i++) {
        Sym *s = &sc->syms[i];
        Node *n;
        int kind = s->dkind == D_OUTPUT ? K_OUTPUT : s->dkind == D_LED ? K_LED
                 : s->dkind == D_SEG7 ? K_SEG7 : s->dkind == D_HEX ? K_HEX : -1;
        if (kind < 0) continue;
        if (!s->expr && !s->nparts)
            fail(s->decl, "%s %.*s is never given a value", dkind_names[s->dkind], NAME(s));
        n = new_node(kind, s->decl);
        name_node(n, s);
        if (kind == K_LED) opt_color(n, s->opts);
        node_in(n, "in", s->net);
        n_out++;
    }
}


/* a component that is never used is still checked: expand it once on
 * scratch nets, then throw the result away */
static void check_unused(void)
{
    int ci, i;
    for (ci = 0; ci < ncomps; ci++) {
        Comp *cp = &comps[ci];
        int outer[MAXIO], inner[MAXIO], nn = nnodes, nt = nnets, ntp = ntaps, ni = ninsts;
        if (cp->used) continue;
        for (i = 0; i < cp->nin; i++) outer[i] = new_net(cp->inw[i]);
        expand(ci, outer, inner, cp->id, NULL);
        nnodes = nn; nnets = nt; ntaps = ntp; ninsts = ni;
    }
}

/* is node i a tri() output, or a chip output that comes from one? */
static int tri_like(int i, const int *drv, int depth)
{
    const Node *n = &nodes[i];
    if (n->kind == K_TRI || n->kind == K_BUS) return 1;
    if (n->kind == K_PIN_OUT && depth < 64) {
        int d = drv[find(n->in[0])];
        return d >= 0 && tri_like(d, drv, depth + 1);
    }
    return 0;
}

/* A bus is one wire driven by several tri() outputs. Each of them gets a
 * wire of its own and one bus part joins them, so every wire still has a
 * single driver; the simulator decides who is talking (and spots two at
 * once). */
static void make_buses(void)
{
    int *drv = (int *)malloc(sizeof(int) * (size_t)(nnets + 1));
    int r, i, j, k;
    for (i = 0; i < nnets; i++) drv[i] = -1;
    for (i = 0; i < nnodes; i++)
        if (!nodes[i].dead) for (j = 0; j < nodes[i].nout; j++) drv[find(nodes[i].out[j])] = i;
    for (r = 0; r < nnets; r++) {
        int list[MAXIO], port[MAXIO], nl = 0, b, w;
        if (find(r) != r || nets[r].drivers < 2) continue;
        for (i = 0; i < nnodes; i++) {
            if (nodes[i].dead) continue;
            for (j = 0; j < nodes[i].nout; j++) {
                if (find(nodes[i].out[j]) != r) continue;
                if (!tri_like(i, drv, 0)) {
                    if (nets[r].name)
                        fail(nets[r].at, "%.*s is driven by %d things at once; only tri() outputs may share "
                             "a wire (declared as a bus), but a %s drives it", nets[r].nlen, nets[r].name,
                             nets[r].drivers, nodes[i].kind == K_PIN_OUT ? "chip output that is not a tri()"
                             : kind_names[nodes[i].kind]);
                    fail(nets[r].at, "a wire is driven by %d things at once", nets[r].drivers);
                }
                if (nl >= MAXIO) fail(nets[r].at, "more than %d things drive one bus", MAXIO);
                list[nl] = i;
                port[nl++] = j;
            }
        }
        w = nets[r].w ? nets[r].w : 1;
        nets[r].w = w;
        cur_inst = nodes[list[0]].kind == K_PIN_OUT ? insts[nodes[list[0]].inst].parent : nodes[list[0]].inst;
        b = nnodes;
        new_node(K_BUS, nets[r].at);
        nodes[b].name = nets[r].name;
        nodes[b].nlen = nets[r].nlen;
        for (k = 0; k < nl; k++) {
            int nt = new_net(w);
            nets[nt].drivers = 1;
            nets[nt].readers = 1;
            nets[nt].at = nets[r].at;
            nodes[list[k]].out[port[k]] = nt;
            node_in(&nodes[b], "", nt);
        }
        node_out(&nodes[b], "out", r);
        nets[r].drivers = 1;
    }
    cur_inst = 0;
    free(drv);
}

/* after every scope is built: share gates, make splitters, check drivers */
static void finish(void)
{
    int i, j;

    /* `~b` written in ten places is one inverter: share identical unnamed
     * gates (same kind, same inputs), until nothing changes */
    for (;;) {
        int merged = 0;
        for (i = 0; i < nnodes; i++) {
            Node *a = &nodes[i];
            if (a->dead || a->name || a->kind > K_BUF) continue;
            for (j = i + 1; j < nnodes; j++) {
                Node *b = &nodes[j];
                int k, eq = 1;
                if (b->dead || b->name || b->kind != a->kind || b->nin != a->nin || b->inst != a->inst) continue;
                for (k = 0; k < a->nin && eq; k++) if (find(a->in[k]) != find(b->in[k])) eq = 0;
                if (!eq) continue;
                join(a->out[0], b->out[0]);
                b->dead = 1;
                merged = 1;
            }
        }
        if (!merged) break;
    }

    /* slices of the same bus become one splitter */
    for (i = 0; i < ntaps; i++) {
        Node *n;
        int src;
        if (taps[i].src < 0) continue;      /* already part of a splitter */
        src = find(taps[i].src);
        cur_inst = taps[i].inst;
        n = new_node(K_SPLIT, taps[i].at);
        node_in(n, "in", src);
        for (j = i; j < ntaps; j++) {
            int k, dup = -1;
            if (taps[j].src < 0 || find(taps[j].src) != src || taps[j].inst != taps[i].inst) continue;
            for (k = 0; k < n->nout; k++)
                if (n->range[k][0] == taps[j].hi && n->range[k][1] == taps[j].lo) dup = k;
            if (dup >= 0) join(n->out[dup], taps[j].out);
            else if (n->nout < MAXIO) {
                n->range[n->nout][0] = taps[j].hi;
                n->range[n->nout][1] = taps[j].lo;
                node_out(n, "", taps[j].out);
            } else fail(taps[j].at, "too many slices of one bus");
            taps[j].src = -1;
        }
    }
    cur_inst = 0;

    /* every net that is read must be driven, by exactly one thing */
    for (i = 0; i < nnodes; i++) {
        if (nodes[i].dead) continue;
        for (j = 0; j < nodes[i].nout; j++) nets[find(nodes[i].out[j])].drivers++;
        for (j = 0; j < nodes[i].nin; j++) nets[find(nodes[i].in[j])].readers++;
    }
    make_buses();
    for (i = 0; i < nnets; i++) {
        Net *n = &nets[i];
        if (find(i) != i) continue;
        if (n->drivers > 1) {
            if (n->name) fail(n->at, "%.*s is driven by %d things at once", n->nlen, n->name, n->drivers);
            fail(n->at, "a wire is driven by %d things at once", n->drivers);
        }
        if (n->readers && !n->drivers) {
            if (n->name) fail(n->at, "%.*s is used but never given a value", n->nlen, n->name);
            fail(n->at, "this output is never given a value");
        }
        if (!n->w) n->w = 1;
    }

    canon = (int *)malloc(sizeof(int) * (size_t)(nnets + 1));
    for (i = 0, j = 0; i < nnets; i++)
        canon[i] = (find(i) == i && (nets[i].drivers || nets[i].readers)) ? j++ : -1;

    {
        int parts = -n_in - n_out, chips = ninsts - 1;
        for (i = 0; i < nnodes; i++) parts += !nodes[i].dead && nodes[i].kind != K_PIN_IN && nodes[i].kind != K_PIN_OUT;
        fprintf(stderr, "%s: %d input%s, %d output%s, %d part%s, %d chip%s, %d wire%s\n", src_name,
                n_in, plural(n_in), n_out, plural(n_out), parts, plural(parts), chips, plural(chips), j, plural(j));
    }
}

/* ---- loading files ------------------------------------------------------ */

static gram_grammar *grammar;
static gram_arena arena;
static gram_node *progs[MAXFILES];
static const char *incdirs[16];
static int nincdirs;
static char exedir[1024];

#ifdef _WIN32
#define PATHSEP ';'
#else
#define PATHSEP ':'
#endif

static int is_abs(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static char *join_path(const char *dir, int dlen, const char *name)
{
    char *p = (char *)malloc((size_t)dlen + strlen(name) + 2);
    memcpy(p, dir, (size_t)dlen);
    if (dlen && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\') p[dlen++] = '/';
    strcpy(p + dlen, name);
    return p;
}

static int dir_len(const char *path)
{
    int i, d = 0;
    for (i = 0; path[i]; i++) if (path[i] == '/' || path[i] == '\\') d = i + 1;
    return d;
}

static void collect_components(int fi);

/* read and parse a file; library files may only hold components and imports */
static int load(const char *path, char *text, size_t len, const gram_node *from)
{
    gram_error err;
    gram_node *st;
    int i, fi;
    for (i = 0; i < nfiles; i++)     /* the same file imported twice, by any path */
        if (files[i].len == len && !memcmp(files[i].text, text, len)) { free(text); return i; }
    if (nfiles >= MAXFILES) fail(from, "too many files imported");
    fi = nfiles++;
    files[fi].name = path;
    files[fi].text = text;
    files[fi].len = len;
    progs[fi] = gram_parse(grammar, NULL, text, len, &arena, &err);
    if (!progs[fi]) { fprintf(stderr, "%s:", path); gram_error_print(&err, put, stderr); exit(1); }
    for (st = progs[fi]->child; st; st = st->next) {
        if (st->rule == WIRE_R_import) {
            char name[1024], *cand[24], *t = NULL;
            int n = 0, k, dl;
            const char *env = getenv("WIRE_PATH");
            size_t l;
            snprintf(name, sizeof name, "%.*s", (int)st->child->len - 2, st->child->text + 1);
            if (is_abs(name)) cand[n++] = join_path("", 0, name);
            else {
                cand[n++] = join_path(path, dir_len(path), name);
                for (k = 0; k < nincdirs; k++) cand[n++] = join_path(incdirs[k], (int)strlen(incdirs[k]), name);
                while (env && *env && n < 20) {
                    const char *e = strchr(env, PATHSEP);
                    dl = e ? (int)(e - env) : (int)strlen(env);
                    if (dl) cand[n++] = join_path(env, dl, name);
                    env = e ? e + 1 : NULL;
                }
                if (exedir[0]) {
                    char lib[1100];
                    snprintf(lib, sizeof lib, "%s../lib", exedir);
                    cand[n++] = join_path(lib, (int)strlen(lib), name);
                    snprintf(lib, sizeof lib, "%s../share/wire/lib", exedir);
                    cand[n++] = join_path(lib, (int)strlen(lib), name);
                }
            }
            for (k = 0; k < n; k++) if ((t = read_file(cand[k], &l)) != NULL) break;
            if (k == n) {
                char looked[4096];
                looked[0] = 0;
                for (k = 0; k < n; k++) {
                    strncat(looked, "\n    ", sizeof looked - strlen(looked) - 1);
                    strncat(looked, cand[k], sizeof looked - strlen(looked) - 1);
                }
                fail(st->child, "cannot find %s; looked for:%s", name, looked);
            }
            load(cand[k], t, l, st->child);
        } else if (fi > 0 && st->rule != WIRE_R_component)
            fail(st, "%s is a library, so it can only hold components and imports. "
                     "Put the circuit in its own file that imports this one.", path);
    }
    collect_components(fi);
    return fi;
}

static void port_decls(Comp *cp, gram_node *list, int out)
{
    gram_node *pd = list->rule == WIRE_R_pdecl ? list : list->child;
    for (; pd; pd = list->rule == WIRE_R_pdecl ? NULL : pd->next) {
        gram_node *id = pd->child, *r = id->next;
        int i, w = 1, *n = out ? &cp->nout : &cp->nin;
        if (*n >= MAXIO) fail(id, "too many ports");
        if ((int)id->len >= NAMEW) fail(id, "port names are at most %d characters", NAMEW - 1);
        for (i = 0; i < cp->nin; i++) if (same(id, cp->inname[i], (int)strlen(cp->inname[i]))) break;
        if (i < cp->nin) fail(id, "%s has two ports called %.*s", cp->name, TEXT(id));
        for (i = 0; i < cp->nout; i++) if (same(id, cp->outname[i], (int)strlen(cp->outname[i]))) break;
        if (i < cp->nout) fail(id, "%s has two ports called %.*s", cp->name, TEXT(id));
        if (r) {
            if (r->child->next) fail(r, "write a port width as [n], e.g. %.*s[8]", TEXT(id));
            w = number(r->child);
            if (w < 1 || w > MAXW) fail(r, "widths go from 1 to %d", MAXW);
        }
        if (out) {
            cp->outid[cp->nout] = id; cp->outw[cp->nout] = w;
            snprintf(cp->outname[cp->nout++], NAMEW, "%.*s", TEXT(id));
        } else {
            cp->inid[cp->nin] = id; cp->inw[cp->nin] = w;
            snprintf(cp->inname[cp->nin++], NAMEW, "%.*s", TEXT(id));
        }
    }
}

static void collect_components(int fi)
{
    gram_node *st;
    for (st = progs[fi]->child; st; st = st->next) {
        gram_node *id;
        Comp *cp;
        int k;
        if (st->rule != WIRE_R_component) continue;
        id = st->child;
        if (kind_of(id) >= 0) fail(id, "%.*s is a built-in part; give your component another name", TEXT(id));
        if ((k = comp_find(id)) >= 0)
            fail(id, "there is already a component called %.*s, in %s line %d", TEXT(id),
                 files[file_of(comps[k].node)].name, line_of(comps[k].node));
        if (ncomps >= MAXCOMPS) fail(id, "too many components");
        cp = &comps[ncomps];
        memset(cp, 0, sizeof *cp);
        cp->name = (char *)malloc(id->len + 1);
        memcpy(cp->name, id->text, id->len);
        cp->name[id->len] = 0;
        cp->node = st;
        cp->id = id;
        port_decls(cp, id->next, 0);
        port_decls(cp, id->next->next, 1);
        if (!cp->nout) fail(id, "%s has no outputs", cp->name);
        cp->body = id->next->next->next;
        ncomps++;
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: wire [-o out.html] [--json] file.wire   (wire --help)\n");
    return 2;
}

int main(int argc, char **argv)
{
    static unsigned char mem[1 << 24];
    gram_error err;
    gram_grammar *g;
    gram_node *prog;
    const char *out = NULL;
    int i, json = 0, tree = 0, check = 0;
    char *text, *def = NULL;
    size_t len;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("wire %s (gram %s)\n", WIRE_VERSION, GRAM_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("wire %s - describe a digital circuit, get an interactive page\n\n"
                   "usage: wire [-o out.html] file.wire\n\n"
                   "  file.wire        writes file.html next to it\n"
                   "  -o out.html      write the page somewhere else\n"
                   "  --json           print the netlist as JSON instead\n"
                   "  --check          only check the circuit\n"
                   "  -I dir           also look for imports in dir (and in $WIRE_PATH)\n"
                   "  -t               print the syntax tree\n"
                   "  --version        print the version\n", WIRE_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--json")) json = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "-t")) tree = 1;
        else if (!strcmp(argv[i], "-I") && i + 1 < argc && nincdirs < 16) incdirs[nincdirs++] = argv[++i];
        else return usage();
    }
    if (i != argc - 1) return usage();
    src_name = argv[i];
    text = read_file(src_name, &len);
    if (!text) { perror(src_name); return 1; }
    {
        int d = dir_len(argv[0]);
        if (d && d < (int)sizeof exedir) { memcpy(exedir, argv[0], (size_t)d); exedir[d] = 0; }
    }

    gram_arena_init(&arena, mem, sizeof mem);
    g = gram_compile(&arena, WIRE_GRAMMAR, &err);
    if (!g) { fputs("grammar:", stderr); gram_error_print(&err, put, stderr); return 1; }
    grammar = g;
    if (tree) {
        prog = gram_parse(g, NULL, text, len, &arena, &err);
        if (!prog) { fprintf(stderr, "%s:", src_name); gram_error_print(&err, put, stderr); return 1; }
        gram_dump(prog, put, stdout);
        return 0;
    }
    load(src_name, text, len, NULL);
    prog = progs[0];

    insts = (Inst *)grow(insts, &capinsts, 1, sizeof *insts);
    memset(&insts[0], 0, sizeof *insts);
    insts[0].comp = -1;
    insts[0].parent = -1;
    ninsts = 1;
    compile_scope(prog->child, 1);
    check_unused();
    finish();
    if (check) return 0;
    if (json) { jf = stdout; write_json(); putchar('\n'); return 0; }
    if (!out) {
        size_t n = strlen(src_name);
        def = (char *)malloc(n + 6);
        strcpy(def, src_name);
        if (n > 5 && !strcmp(def + n - 5, ".wire")) def[n - 5] = 0;
        strcat(def, ".html");
        out = def;
    }
    if (!write_html(out)) { perror(out); return 1; }
    fprintf(stderr, "wrote %s\n", out);
    return 0;
}
