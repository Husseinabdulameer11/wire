/* wire - a language for digital circuits, built with gram.
 *
 *   wire circuit.wire              writes circuit.html
 *   wire -o page.html circuit.wire
 *   wire --json circuit.wire       prints the netlist
 *
 * The program is read in three passes:
 *   1. collect   every name (declarations and assignment targets) gets a net
 *   2. widths    bit widths are inferred, so only inputs need `[n]`
 *   3. build     expressions become components (nodes) and nets; slices of
 *                the same bus are grouped into one splitter
 * The netlist is then written as JSON into a copy of the viewer page
 * (wire_view.html), which lays the circuit out, draws and simulates it. */
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

static const char *src_name, *src_text;
static size_t src_len;

/* ---- errors ------------------------------------------------------------ */

static void fail(const gram_node *at, const char *f, ...)
{
    va_list ap;
    size_t line = 0, col = 0;
    if (at) gram_locate(src_text, src_len, (size_t)(at->text - src_text), &line, &col);
    fprintf(stderr, "%s:", src_name);
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
    if (!at) return 0;
    gram_locate(src_text, src_len, (size_t)(at->text - src_text), &line, NULL);
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
    /* not callable: */
    K_INPUT, K_BUTTON, K_CLOCK, K_OUTPUT, K_LED, K_SEG7, K_HEX,
    K_CONST, K_SPLIT, K_MERGE, K_COUNT
};

static const char *kind_names[K_COUNT] = {
    "and", "or", "nand", "nor", "xor", "xnor", "not", "buf",
    "mux", "demux", "decoder", "encoder", "add", "compare",
    "dff", "tff", "jkff", "counter",
    "input", "button", "clock", "output", "led", "seg7", "hex",
    "const", "split", "merge"
};

typedef struct {
    int kind;
    const char *name; int nlen;   /* label: the name it was assigned to */
    int line;
    int nin, nout;
    int in[MAXIO], out[MAXIO];
    char inname[MAXIO][8], outname[MAXIO][8];
    int range[MAXIO][2];          /* split: per output, merge: per input */
    unsigned long value;          /* const */
    int bits;                     /* counter */
    const char *opt; int optlen;  /* led colour */
    long hz;                      /* clock, in hundredths */
    int dead;                     /* merged into an identical gate */
} Node;

static Node *nodes; static int nnodes, capnodes;

static Node *new_node(int kind, const gram_node *at)
{
    Node *n;
    nodes = (Node *)grow(nodes, &capnodes, nnodes + 1, sizeof *nodes);
    n = &nodes[nnodes++];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->line = line_of(at);
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

enum { D_NONE, D_INPUT, D_OUTPUT, D_WIRE, D_LED, D_SEG7, D_HEX, D_BUTTON, D_CLOCK };
static const char *dkind_names[] = {
    "", "input", "output", "wire", "led", "seg7", "hex", "button", "clock"
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

static Sym *syms; static int nsyms, capsyms;

static int same(const gram_node *id, const char *s, int len)
{
    return (int)id->len == len && !memcmp(id->text, s, (size_t)len);
}

static Sym *lookup(const gram_node *id)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (same(id, syms[i].name, syms[i].len)) return &syms[i];
    return NULL;
}

static Sym *intern(gram_node *id)
{
    Sym *s = lookup(id);
    if (s) return s;
    syms = (Sym *)grow(syms, &capsyms, nsyms + 1, sizeof *syms);
    s = &syms[nsyms++];
    memset(s, 0, sizeof *s);
    s->name = id->text;
    s->len = (int)id->len;
    s->net = -1;
    s->pre = -1;
    return s;
}

#define NAME(s) (s)->len, (s)->name
#define TEXT(n) (int)(n)->len, (n)->text

/* `c.cout`, `dm[2]`: output ports of a named component, created on first use */
typedef struct { Sym *sym; char port[8]; int net; gram_node *at; } PortRef;
static PortRef *prefs; static int nprefs, capprefs;

static gram_node *sym_call(Sym *s);
static int is_default_port(gram_node *call, const char *port);

static int port_net(Sym *s, const char *port, gram_node *at)
{
    int i;
    if (sym_call(s) && is_default_port(sym_call(s), port)) return s->net;
    for (i = 0; i < nprefs; i++)
        if (prefs[i].sym == s && !strcmp(prefs[i].port, port)) return prefs[i].net;
    prefs = (PortRef *)grow(prefs, &capprefs, nprefs + 1, sizeof *prefs);
    prefs[nprefs].sym = s;
    strcpy(prefs[nprefs].port, port);
    prefs[nprefs].net = new_net(0);
    prefs[nprefs].at = at;
    return prefs[nprefs++].net;
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
    int bits;
    /* computed: */
    int nin; gram_node *in[MAXIO]; char inname[MAXIO][8]; int inw[MAXIO];
    int nout; char outname[MAXIO][8]; int outw[MAXIO];
    int defport;   /* output used when the component is used as a value, -1 none */
    int indexed;   /* outputs o0..oN: `x[i]` picks one */
} Call;

static int ew(gram_node *e);
static int sym_width(Sym *s);

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
        fail(c->call, "%s takes %s, not %d", kind_names[c->kind], how, c->npos);
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
             what, kind_names[c->kind], w, plural(w), TEXT(e), aw, plural(aw));
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
    if (c->kind < 0) {
        say_kinds(buf);
        fail(c->id, "there is no component called %.*s (there are: %s)", TEXT(c->id), buf);
    }
    for (a = c->id->next; a; a = a->next) {
        if (a->rule == WIRE_R_named) {
            gram_node *k = a->child, *v = a->child->next;
            if (same(k, "sel", 3) && (c->kind == K_MUX || c->kind == K_DEMUX)) c->sel = v;
            else if (same(k, "en", 2) && (c->kind == K_DECODER || c->kind == K_DFF || c->kind == K_COUNTER)) c->en = v;
            else if (same(k, "rst", 3) && c->kind >= K_DFF && c->kind <= K_COUNTER) c->rst = v;
            else if (same(k, "cin", 3) && c->kind == K_ADD) c->cin = v;
            else if (same(k, "bits", 4) && c->kind == K_COUNTER) {
                if (v->rule != WIRE_R_number) fail(v, "bits must be a number");
                c->bits = number(v);
                if (c->bits < 1 || c->bits > MAXW) fail(v, "a counter has 1 to %d bits", MAXW);
            } else fail(k, "%s has no setting called %.*s", kind_names[c->kind], TEXT(k));
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
        w = c->bits ? c->bits : 4;
        if (check) want_width(c, c->pos[0], 1, "the clock");
        add_in(c, c->pos[0], "clk", 1);
    flags:
        if (c->en)  { if (check) want_width(c, c->en, 1, "en");   add_in(c, c->en, "en", 1); }
        if (c->rst) { if (check) want_width(c, c->rst, 1, "rst"); add_in(c, c->rst, "rst", 1); }
        if (c->kind == K_COUNTER) { add_out(c, "q", w); add_out(c, "carry", 1); }
        else { add_out(c, "q", w); add_out(c, "nq", w); }
        break;
    }
    if (check)
        for (i = 0; i < c->nout; i++)
            if (c->outw[i] > MAXW) fail(c->call, "%s would be %d bits wide; the limit is %d",
                                        kind_names[c->kind], c->outw[i], MAXW);
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
                char p[8];
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

typedef struct { int src, hi, lo, out; const gram_node *at; } Tap;
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

/* build a call; the output in `dst` is the default port, other ports of a
 * named component (`s`) use the nets made for `s.port` references */
static Node *build_call(gram_node *call, int dst, Sym *s)
{
    Call c;
    Node *n;
    int i, k;
    analyze(call, &c, 1);
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
        char p[8], buf[200];
        Call cl;
        if (!call) fail(c, "%.*s is not a component, so it has no .%.*s",
                        TEXT(base), TEXT(c->child));
        analyze(call, &cl, 0);
        snprintf(p, sizeof p, "%.*s", TEXT(c->child));
        if (c->child->len >= sizeof p || find_port(&cl, p) < 0) {
            port_list(&cl, buf);
            fail(c->child, "%s has no output called %.*s; it has %s",
                 kind_names[cl.kind], TEXT(c->child), buf);
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
            char p[8];
            int i = number(c->child);
            if (c->child->next) fail(c, "pick one output of %s: [i], not a range", kind_names[cl.kind]);
            if (i >= cl.nout) fail(c, "%s has outputs 0 to %d", kind_names[cl.kind], cl.nout - 1);
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
            char buf[200];
            analyze(call, &cl, 0);
            if (cl.defport < 0) {
                port_list(&cl, buf);
                fail(e, "%.*s is a %s with several outputs; pick one: %s",
                     TEXT(e), kind_names[cl.kind], buf);
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
        char buf[200];
        analyze(e, &cl, 0);
        if (cl.defport < 0) {
            port_list(&cl, buf);
            fail(e, "%s has several outputs; pick one with .%s (outputs: %s)",
                 kind_names[cl.kind], cl.outname[0], buf);
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
    for (dk = 1; dk <= D_CLOCK; dk++)
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
    if (s->dkind == D_INPUT || s->dkind == D_BUTTON || s->dkind == D_CLOCK)
        fail(id, "%.*s is an %s; it gets its value from the page, not from the circuit",
             TEXT(id), dkind_names[s->dkind]);
    if (!r) {
        if (s->expr)
            fail(id, "%.*s is already given a value on line %d", TEXT(id), line_of(s->at));
        if (s->nparts)
            fail(id, "%.*s already has single bits assigned on line %d", TEXT(id), line_of(s->parts[0].at));
        s->expr = expr;
        s->at = at;
        s->conn = pre == -2;
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
    jstr(src_text, (int)src_len);
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
        fprintf(jf, ",\"line\":%d,\"in\":[", n->line);
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

static void build_all(gram_node *prog)
{
    gram_node *st;
    int i, j, in = 0, out = 0;

    /* 1. names */
    for (st = prog->child; st; st = st->next) {
        if (st->rule == WIRE_R_title) {
            if (title) fail(st, "the circuit already has a name");
            title = st->child->text; titlelen = (int)st->child->len;
        } else if (st->rule == WIRE_R_decl) declare(st);
    }
    for (st = prog->child; st; st = st->next) {
        if (st->rule == WIRE_R_assign) assign_to(st->child, st->child->next, st, -1);
        else if (st->rule == WIRE_R_connect) {
            gram_node *t;
            for (t = st->child->next; t; t = t->next) assign_to(t, st->child, st, -2); /* built later */
        }
    }
    for (i = 0; i < nsyms; i++) {
        syms[i].net = new_net(0);
        nets[syms[i].net].name = syms[i].name;
        nets[syms[i].net].nlen = syms[i].len;
        nets[syms[i].net].at = syms[i].decl;
    }

    /* 2. widths */
    for (i = 0; i < nsyms; i++) sym_width(&syms[i]);
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
        if (s->dkind == D_SEG7 && s->w != 7 && s->w != 8)
            fail(s->decl, "a seg7 needs 7 bits (segments a..g) or 8 (with the dot), not %d", s->w);
        if (s->dkind == D_HEX && s->w > 4) fail(s->decl, "a hex display shows 4 bits, not %d", s->w);
        if (s->w > MAXW) fail(s->decl, "%.*s is %d bits; the limit is %d", NAME(s), s->w, MAXW);
        if (!sym_call(s) || s->w) nets[s->net].w = s->w;
    }

    /* sources: the page drives these */
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
        Node *n;
        int kind = s->dkind == D_INPUT ? K_INPUT : s->dkind == D_BUTTON ? K_BUTTON
                 : s->dkind == D_CLOCK ? K_CLOCK : -1;
        if (kind < 0) continue;
        n = new_node(kind, s->decl);
        name_node(n, s);
        if (kind == K_CLOCK) n->hz = opt_hz(s->opts);
        node_out(n, "out", s->net);
        in++;
    }

    /* 3. build, in the order written */
    for (st = prog->child; st; st = st->next) {
        if (st->rule == WIRE_R_assign) {
            gram_node *t = st->child, *e = t->next;
            Sym *s = lookup(t->child);
            if (!t->child->next) {
                int ew_ = is_const(e) ? 0 : ew(e);
                if (s->w && ew_ && s->w != ew_ && !sym_call(s))
                    fail(e, "%.*s is %d bit%s, but this is %d bit%s", NAME(s), s->w, plural(s->w), ew_, plural(ew_));
                if (e->rule == WIRE_R_call) {
                    Call cl;
                    analyze(e, &cl, 0);
                    if (cl.defport < 0 && s->dkind != D_NONE) build(e, s->net, s->w);  /* says: pick one */
                    build_call(e, cl.defport >= 0 ? s->net : -1, s);
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
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
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
    for (i = 0; i < nprefs; i++) {
        gram_node *call = sym_call(prefs[i].sym);
        Call cl;
        char buf[200];
        if (!call) fail(prefs[i].at, "%.*s is not a component", NAME(prefs[i].sym));
        analyze(call, &cl, 0);
        if (find_port(&cl, prefs[i].port) < 0) {
            port_list(&cl, buf);
            fail(prefs[i].at, "%s has no output called %s; it has %s", kind_names[cl.kind], prefs[i].port, buf);
        }
    }

    /* sinks: the page shows these */
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
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
        out++;
    }

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
                if (b->dead || b->name || b->kind != a->kind || b->nin != a->nin) continue;
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
        int src = find(taps[i].src);
        if (taps[i].src < 0) continue;
        n = new_node(K_SPLIT, taps[i].at);
        node_in(n, "in", src);
        for (j = i; j < ntaps; j++) {
            int k, dup = -1;
            if (taps[j].src < 0 || find(taps[j].src) != src) continue;
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

    /* every net that is read must be driven, by exactly one thing */
    for (i = 0; i < nnodes; i++) {
        if (nodes[i].dead) continue;
        for (j = 0; j < nodes[i].nout; j++) nets[find(nodes[i].out[j])].drivers++;
        for (j = 0; j < nodes[i].nin; j++) nets[find(nodes[i].in[j])].readers++;
    }
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
        int comps = -in - out;
        for (i = 0; i < nnodes; i++) comps += !nodes[i].dead;
        fprintf(stderr, "%s: %d input%s, %d output%s, %d part%s, %d wire%s\n", src_name,
                in, plural(in), out, plural(out), comps, plural(comps), j, plural(j));
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: wire [-o out.html] [--json] file.wire   (wire --help)\n");
    return 2;
}

int main(int argc, char **argv)
{
    static gram_arena arena;
    static unsigned char mem[1 << 22];
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
                   "  -t               print the syntax tree\n"
                   "  --version        print the version\n", WIRE_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--json")) json = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "-t")) tree = 1;
        else return usage();
    }
    if (i != argc - 1) return usage();
    src_name = argv[i];
    text = read_file(src_name, &len);
    if (!text) { perror(src_name); return 1; }
    src_text = text;
    src_len = len;

    gram_arena_init(&arena, mem, sizeof mem);
    g = gram_compile(&arena, WIRE_GRAMMAR, &err);
    if (!g) { fputs("grammar:", stderr); gram_error_print(&err, put, stderr); return 1; }
    prog = gram_parse(g, NULL, text, len, &arena, &err);
    if (!prog) { fprintf(stderr, "%s:", src_name); gram_error_print(&err, put, stderr); return 1; }
    if (tree) { gram_dump(prog, put, stdout); return 0; }

    build_all(prog);
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
