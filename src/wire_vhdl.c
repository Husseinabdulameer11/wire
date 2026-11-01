/* wire_vhdl.c - VHDL for any circuit. Included by wire.c (it reads the
 * netlist that wire.c builds), so it is not compiled on its own.
 *
 *   wire --to-vhdl circuit.wire            prints circuit's VHDL
 *   wire --to-vhdl -o circuit.vhd c.wire   writes it to a file
 *
 * A page also shows this VHDL when there is no .vhd next to the .wire file.
 *
 * The VHDL is VHDL-93 (what Vivado uses for a .vhd file unless told
 * otherwise), so it never reads an `out` port and lists every process's
 * signals by hand. Every component becomes an entity, instantiated where it
 * is used, and the circuit itself becomes an entity named like the file,
 * with its inputs, buttons, keypads and clocks as `in` ports and its
 * outputs, LEDs and displays as `out` ports.
 *
 * It behaves like Wire's simulator: flip-flops, counters and ram start at 0
 * (initial values), rst is asynchronous, a flip-flop with en only loads
 * while en is 1, a counter's load works without en, tri() drives 'Z' and a
 * bus is a resolved signal with one driver per tri(). Loops made of gates
 * (an SR latch) start in the state Wire's simulator settles to, worked out
 * here, so the VHDL powers up the same way. tests/vhdl/cosim.js --generated
 * runs every circuit both ways in GHDL and checks they agree. */

/* ---- text ---------------------------------------------------------------- */

typedef struct { char *s; size_t n, cap; } VBuf;

static void vb_add(VBuf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2 + 256;
        b->s = (char *)realloc(b->s, b->cap);
        if (!b->s) { fputs("wire: out of memory\n", stderr); exit(1); }
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

static void vbf(VBuf *b, const char *f, ...)
{
    char tmp[4096];
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(tmp, sizeof tmp, f, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { vb_add(b, tmp, (size_t)n); return; }
    {
        char *big = (char *)malloc((size_t)n + 1);
        va_start(ap, f);
        vsnprintf(big, (size_t)n + 1, f, ap);
        va_end(ap);
        vb_add(b, big, (size_t)n);
        free(big);
    }
}

static char *vdup(const char *s)
{
    char *d = (char *)malloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

/* ---- names --------------------------------------------------------------- */

/* VHDL words (VHDL-2008 included, so the file also reads as 2008), and the
 * names the generated code itself uses */
static const char *vhdl_words[] = {
    "abs", "access", "after", "alias", "all", "and", "architecture", "array", "assert",
    "assume", "assume_guarantee", "attribute", "begin", "block", "body", "buffer", "bus",
    "case", "component", "configuration", "constant", "context", "cover", "default",
    "disconnect", "downto", "else", "elsif", "end", "entity", "exit", "fairness", "file",
    "for", "force", "function", "generate", "generic", "group", "guarded", "if", "impure",
    "in", "inertial", "inout", "is", "label", "library", "linkage", "literal", "loop", "map",
    "mod", "nand", "new", "next", "nor", "not", "null", "of", "on", "open", "or", "others",
    "out", "package", "parameter", "port", "postponed", "procedure", "process", "property",
    "protected", "pure", "range", "record", "register", "reject", "release", "rem", "report",
    "restrict", "restrict_guarantee", "return", "rol", "ror", "select", "sequence", "severity",
    "shared", "signal", "sla", "sll", "sra", "srl", "strong", "subtype", "then", "to",
    "transport", "type", "unaffected", "units", "until", "use", "variable", "vmode", "vprop",
    "vunit", "wait", "when", "while", "with", "xnor", "xor",
    "ieee", "std", "work", "std_logic", "std_logic_vector", "std_ulogic", "unsigned", "signed",
    "numeric_std", "std_logic_1164", "rising_edge", "falling_edge", "resize", "to_integer",
    "to_unsigned", "natural", "integer", "boolean", "true", "false", "bit", "string",
    "character", "time", "now", NULL
};

static int is_vhdl_word(const char *s)
{
    int i;
    for (i = 0; vhdl_words[i]; i++) {
        const char *a = vhdl_words[i], *b = s;
        while (*a && *b && *a == (char)(*b | 32 * (*b >= 'A' && *b <= 'Z'))) a++, b++;
        if (!*a && !*b) return 1;
    }
    return 0;
}

/* a Wire name as a VHDL identifier: a letter first, no "__", no "_" at the
 * end, and not a VHDL word */
static void vhdl_ident(const char *s, int len, char *out, size_t cap)
{
    size_t n = 0;
    int i;
    for (i = 0; i < len && n + 4 < cap; i++) {
        char c = s[i];
        if (c == '_' && (n == 0 || out[n - 1] == '_')) continue;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) continue;
        if (n == 0 && c >= '0' && c <= '9') out[n++] = 'n';
        out[n++] = c;
    }
    while (n && out[n - 1] == '_') n--;
    if (!n) out[n++] = 'n';
    out[n] = 0;
    if (is_vhdl_word(out) && n + 3 < cap) strcpy(out + n, "_s");
}

/* the names taken in one entity (VHDL names ignore case) */
static char **vtaken;
static int nvtaken, capvtaken;

static int vname_taken(const char *s)
{
    int i;
    for (i = 0; i < nvtaken; i++) {
        const char *a = vtaken[i], *b = s;
        while (*a && *b && (*a | 32) == (*b | 32)) a++, b++;
        if (!*a && !*b) return 1;
    }
    return 0;
}

static char *vname_take(const char *want, const char *suffix)
{
    char buf[256];
    int k;
    snprintf(buf, sizeof buf, "%s", want);
    if (vname_taken(buf) && suffix) snprintf(buf, sizeof buf, "%s_%s", want, suffix);
    for (k = 2; vname_taken(buf); k++) snprintf(buf, sizeof buf, "%s_%d", want, k);
    vtaken = (char **)grow(vtaken, &capvtaken, nvtaken + 1, sizeof *vtaken);
    return vtaken[nvtaken++] = vdup(buf);
}

/* ---- the scope being written ------------------------------------------- */

static char **vn;          /* net -> its VHDL name, or a literal for a constant */
static char **vlabs;       /* instance -> its label in the entity being written */
static int *vreaders;      /* net -> parts reading it in this entity */
static int *vdriver;       /* net -> the part writing it (-1: none here) */
static char *vsignal;      /* net -> 1: declare it as a signal */
static char *vportnet;     /* net -> 1: it is a port of this entity */
static unsigned long *vinitv; /* net -> value it starts with (loops, flip-flops) */
static char *vhasinit;
static char *vmayx;        /* net -> can float or be unknown (fed by tri() or a bus) */
static char vk01[200];     /* this entity's known01 function, once it needs one */
static int vnamed;         /* counter for nets with no name */
static int vrams;
static int vscreens;               /* attribute wire_screen declared in this architecture */
static char *vcpu_text;            /* the signal the cpu's program rom drives, in VHDL */
static char *vcpu_lane[64];        /* the VHDL arrays of its data rams, most significant first */
static void vemit_cpu_attr(VBuf *decl);
static int vtexts;                 /* ... and wire_text */

static int net_w(int net) { return nets[find(net)].w ? nets[find(net)].w : 1; }

static const char *vtype(int w, char *buf, size_t cap)
{
    if (w == 1) snprintf(buf, cap, "std_logic");
    else snprintf(buf, cap, "std_logic_vector(%d downto 0)", w - 1);
    return buf;
}

/* a value as a literal of w bits: '1', or "0101" */
static const char *vlit(unsigned long v, int w, char *buf)
{
    int b, i = 0;
    if (w == 1) { sprintf(buf, "'%lu'", v & 1); return buf; }
    buf[i++] = '"';
    for (b = w - 1; b >= 0; b--) buf[i++] = (char)('0' + ((v >> b) & 1));
    buf[i++] = '"';
    buf[i] = 0;
    return buf;
}

static int vis_const(int net) { return vdriver[net] >= 0 && nodes[vdriver[net]].kind == K_CONST; }

static const char *N(int net) { return vn[find(net)] ? vn[find(net)] : "open"; }

/* the net as an unsigned expression (numeric_std) */
static const char *U(int net, char *buf, size_t cap)
{
    net = find(net);
    if (vis_const(net)) snprintf(buf, cap, net_w(net) == 1 ? "unsigned'(0 => %s)" : "unsigned'(%s)", N(net));
    else if (net_w(net) == 1) snprintf(buf, cap, "unsigned'(0 => %s)", N(net));
    else snprintf(buf, cap, "unsigned(%s)", N(net));
    return buf;
}

/* "x = '1'" as a condition; a constant becomes true or false (VHDL cannot
 * compare two literals: '1' = '1' is ambiguous) */
static const char *C(int net, int is1)
{
    static char ring[8][300];
    static int r;
    char *b = ring[r++ & 7];
    net = find(net);
    if (vis_const(net)) snprintf(b, 300, "%s", ((nodes[vdriver[net]].value & 1) != 0) == is1 ? "true" : "false");
    else snprintf(b, 300, "%s %s '1'", N(net), is1 ? "=" : "/=");
    return b;
}

/* a value about to be stored: Wire stores a bit that nothing drives, or that
 * is unknown, as 0; known01() does the same, only where it can happen */
static const char *K01(int net)
{
    static char ring[8][300];
    static int r;
    char *b;
    net = find(net);
    if (!vmayx[net] || vis_const(net)) return N(net);
    if (!vk01[0]) snprintf(vk01, sizeof vk01, "%s", vname_take("known01", NULL));
    b = ring[r++ & 7];
    snprintf(b, 300, "%s(%s)", vk01, N(net));
    return b;
}

static int vin_by(const Node *n, const char *name)
{
    int j;
    for (j = 0; j < n->nin; j++) if (!strcmp(n->inname[j], name)) return find(n->in[j]);
    return -1;
}

static int is_seq(int kind) { return kind == K_DFF || kind == K_TFF || kind == K_JKFF || kind == K_COUNTER; }
static int is_pin(int kind) { return kind == K_PIN_IN || kind == K_PIN_OUT; }
static int is_source(int kind) { return kind == K_INPUT || kind == K_BUTTON || kind == K_CLOCK || kind == K_KEYPAD; }
static int is_sink(int kind) { return kind == K_OUTPUT || kind == K_LED || kind == K_SEG7 || kind == K_HEX; }

/* ---- the state Wire powers up in ----------------------------------------
 * Wire's simulator starts with every input and flip-flop at 0 and settles
 * the logic; loops of gates (an SR latch) are settled in a fixed order, which
 * decides which side wins. The VHDL signals of such loops get those values
 * as initial values, or GHDL would see them toggle forever at time 0. This
 * is a plain C version of that settle: values and known masks per net,
 * evaluated in the simulator's order (wire_sim.js: classicOrder, then
 * strongly connected components). */

static unsigned long *sv, *sk;      /* value and known mask per net */
static char *in_loop;               /* net -> driven by a part in a loop of gates */

static unsigned long wmask(int w) { return w >= 32 ? 0xFFFFFFFFul : ((1ul << w) - 1); }

static void sim_out(int net, unsigned long v, unsigned long k, int keepz, int *changed)
{
    unsigned long m = wmask(net_w(net));
    net = find(net);
    k &= m;
    if (keepz) v &= m; else v &= k;
    if (sv[net] != v || sk[net] != k) { sv[net] = v; sk[net] = k; if (changed) *changed = 1; }
}

static int sim_known(int net) { net = find(net); return sk[net] == wmask(net_w(net)); }

/* a times b, w bits each (sgn: 1 a is signed, 2 b is signed), the 2w-bit product */
static unsigned long long mul_product(unsigned long a, unsigned long b, int w, int sgn)
{
    unsigned long long m = w >= 32 ? 0xffffffffull : (1ull << w) - 1;
    long long x = (long long)(a & m), y = (long long)(b & m);
    if ((sgn & 1) && (x >> (w - 1) & 1)) x -= (long long)(m + 1);
    if ((sgn & 2) && (y >> (w - 1) & 1)) y -= (long long)(m + 1);
    return ((unsigned long long)x * (unsigned long long)y) & (w >= 32 ? ~0ull : (1ull << 2 * w) - 1);
}

static void sim_node(int j, int *changed)
{
    Node *n = &nodes[j];
    int i;
    unsigned long v, k;
#define IN(i) find(n->in[i])
#define OUT(i) find(n->out[i])
    switch (n->kind) {
    case K_INPUT: case K_BUTTON: case K_CLOCK: case K_KEYPAD:
        sim_out(OUT(0), 0, ~0ul, 0, changed); return;
    case K_CONST:
        sim_out(OUT(0), n->value, ~0ul, 0, changed); return;
    case K_AND: case K_NAND: case K_OR: case K_NOR: case K_XOR: case K_XNOR:
        v = sv[IN(0)]; k = sk[IN(0)];
        for (i = 1; i < n->nin; i++) {
            unsigned long a = sv[IN(i)], b = sk[IN(i)], nk;
            if (n->kind == K_AND || n->kind == K_NAND) { nk = (k & b) | (k & ~v) | (b & ~a); v = v & a & nk; k = nk; }
            else if (n->kind == K_OR || n->kind == K_NOR) { nk = (k & b) | (k & v) | (b & a); v = (v | a) & nk; k = nk; }
            else { k &= b; v = (v ^ a) & k; }
        }
        if (n->kind == K_NAND || n->kind == K_NOR || n->kind == K_XNOR) v = ~v & k;
        sim_out(OUT(0), v, k, 0, changed); return;
    case K_NOT:
        sim_out(OUT(0), ~sv[IN(0)] & sk[IN(0)], sk[IN(0)], 0, changed); return;
    case K_BUF: case K_PIN_IN: case K_PIN_OUT:
        sim_out(OUT(0), sv[IN(0)], sk[IN(0)], 1, changed); return;
    case K_TRI:
        if (!sim_known(IN(1))) sim_out(OUT(0), 0, 0, 1, changed);
        else if (sv[IN(1)]) sim_out(OUT(0), sv[IN(0)] & sk[IN(0)], sk[IN(0)], 1, changed);
        else sim_out(OUT(0), ~0ul, 0, 1, changed);
        return;
    case K_BUS: {
        unsigned long d1 = 0, d2 = 0, kd = 0, m = wmask(net_w(OUT(0)));
        v = 0;
        for (i = 0; i < n->nin; i++) {
            unsigned long t = ~(sv[IN(i)] & ~sk[IN(i)]) & m;
            d2 |= d1 & t; d1 |= t;
            v |= sv[IN(i)] & sk[IN(i)] & t;
            kd |= sk[IN(i)] & t;
        }
        k = kd & d1 & ~d2;
        v = (v & k) | (~d1 & m);
        sim_out(OUT(0), v, k, 1, changed); return;
    }
    case K_ROM: case K_RAM: {
        int a = vin_by(n, "addr");
        if (a >= 0 && sim_known(a)) {
            unsigned long ad = sv[a];
            sim_out(OUT(0), (long)ad < n->memlen ? n->mem[ad] : 0, ~0ul, 0, changed);
        } else sim_out(OUT(0), 0, 0, 0, changed);
        return;
    }
    case K_MUX: {
        int sel = IN(n->nin - 1);
        if (sim_known(sel) && (int)sv[sel] < n->nin - 1) sim_out(OUT(0), sv[IN(sv[sel])], sk[IN(sv[sel])], 0, changed);
        else sim_out(OUT(0), 0, 0, 0, changed);
        return;
    }
    case K_DEMUX:
        for (i = 0; i < n->nout; i++) {
            if (!sim_known(IN(1))) sim_out(OUT(i), 0, 0, 0, changed);
            else if (sv[IN(1)] == (unsigned long)i) sim_out(OUT(i), sv[IN(0)], sk[IN(0)], 0, changed);
            else sim_out(OUT(i), 0, ~0ul, 0, changed);
        }
        return;
    case K_DECODER: {
        int en = vin_by(n, "en"), sel = IN(0);
        if (en >= 0 && sim_known(en) && !sv[en]) sim_out(OUT(0), 0, ~0ul, 0, changed);
        else if (sim_known(sel) && (en < 0 || sim_known(en))) sim_out(OUT(0), 1ul << sv[sel], ~0ul, 0, changed);
        else sim_out(OUT(0), 0, 0, 0, changed);
        return;
    }
    case K_ENCODER:
        if (sim_known(IN(0))) {
            unsigned long t = sv[IN(0)];
            int hi = 0;
            for (i = 0; i < 32; i++) if (t >> i & 1) hi = i;
            sim_out(OUT(0), (unsigned long)hi, ~0ul, 0, changed);
            sim_out(OUT(1), t != 0, ~0ul, 0, changed);
        } else { sim_out(OUT(0), 0, 0, 0, changed); sim_out(OUT(1), 0, 0, 0, changed); }
        return;
    case K_ADD: {
        int c = vin_by(n, "cin"), w = net_w(IN(0));
        if (sim_known(IN(0)) && sim_known(IN(1)) && (c < 0 || sim_known(c))) {
            unsigned long long s = (unsigned long long)sv[IN(0)] + sv[IN(1)] + (c >= 0 ? sv[c] : 0);
            sim_out(OUT(0), (unsigned long)s, ~0ul, 0, changed);
            sim_out(OUT(1), (unsigned long)(s >> w) & 1, ~0ul, 0, changed);
        } else { sim_out(OUT(0), 0, 0, 0, changed); sim_out(OUT(1), 0, 0, 0, changed); }
        return;
    }
    case K_MUL: {
        int w = net_w(IN(0));
        if (sim_known(IN(0)) && sim_known(IN(1))) {
            unsigned long long p = mul_product(sv[IN(0)], sv[IN(1)], w, n->sgn);
            sim_out(OUT(0), (unsigned long)(p & 0xffffffffull), ~0ul, 0, changed);
            sim_out(OUT(1), (unsigned long)(p >> w), ~0ul, 0, changed);
        } else { sim_out(OUT(0), 0, 0, 0, changed); sim_out(OUT(1), 0, 0, 0, changed); }
        return;
    }
    case K_COMPARE:
        for (i = 0; i < 3; i++) {
            if (sim_known(IN(0)) && sim_known(IN(1))) {
                unsigned long a = sv[IN(0)], b = sv[IN(1)];
                sim_out(OUT(i), i == 0 ? a < b : i == 1 ? a == b : a > b, ~0ul, 0, changed);
            } else sim_out(OUT(i), 0, 0, 0, changed);
        }
        return;
    case K_DFF: case K_TFF: case K_JKFF: case K_COUNTER: {
        /* state 0 at power-up */
        int w = net_w(OUT(0));
        sim_out(OUT(0), 0, ~0ul, 0, changed);
        if (n->kind == K_COUNTER) {
            int en = vin_by(n, "en"), dn = vin_by(n, "down");
            unsigned long c = (dn >= 0 && sim_known(dn) && sv[dn]) ? 1 : (wmask(w) == 0);
            if (en >= 0 && !(sim_known(en) && sv[en])) c = 0;
            if (n->nout > 1) sim_out(OUT(1), c, ~0ul, 0, changed);
        } else if (n->nout > 1) sim_out(OUT(1), wmask(w), ~0ul, 0, changed);
        return;
    }
    case K_SPLIT:
        for (i = 0; i < n->nout; i++) {
            int hi = n->range[i][0], lo = n->range[i][1];
            unsigned long m = wmask(hi - lo + 1);
            sim_out(OUT(i), (sv[IN(0)] >> lo) & m, (sk[IN(0)] >> lo) & m, 1, changed);
        }
        return;
    case K_MERGE:
        v = 0; k = 0;
        for (i = 0; i < n->nin; i++) { v |= sv[IN(i)] << n->range[i][1]; k |= sk[IN(i)] << n->range[i][1]; }
        sim_out(OUT(0), v, k, 1, changed);
        return;
    default:
        return;
    }
#undef IN
#undef OUT
}

/* does the reader use this input at once (not only at a clock edge)? For a
 * flip-flop only rst does (and en, down for a counter's carry); for a ram,
 * addr (reading) */
static int comb_edge(const Node *rd, int drvnode, const int *drv)
{
    int k;
    if (!is_seq(rd->kind) && rd->kind != K_RAM) return 1;
    for (k = 0; k < rd->nin; k++) {
        const char *nm;
        if (drv[find(rd->in[k])] != drvnode) continue;
        nm = rd->inname[k];
        if (!strcmp(nm, "rst")) return 1;
        if (rd->kind == K_COUNTER && (!strcmp(nm, "en") || !strcmp(nm, "down"))) return 1;
        if (rd->kind == K_RAM && !strcmp(nm, "addr")) return 1;
    }
    return 0;
}

/* the order the simulator uses (wire_sim.js classicOrder): depth first from
 * the sources marks the edges that close loops, then a topological sort */
static void power_up(void)
{
    int N = nnodes, i, j, e, ne = 0, *drv, *ea, *eb, *outdeg, *outstart, *outlist, *state, *rank, *indeg, *q;
    char *back;
    int qh = 0, qt = 0, pos = 0;
    int *idx, *low, *stk, *onstk, *work, *wpos, sp = 0, wp = 0, counter = 0, *comp_of, ncomp = 0;
    int *order, norder = 0;

    sv = (unsigned long *)calloc((size_t)nnets + 1, sizeof *sv);
    sk = (unsigned long *)calloc((size_t)nnets + 1, sizeof *sk);
    in_loop = (char *)calloc((size_t)nnets + 1, 1);
    /* like the simulator (and the cosim test) start: every wire a known 0 */
    for (i = 0; i < nnets; i++) { sv[i] = 0; sk[i] = wmask(net_w(i)); }

    drv = (int *)malloc(sizeof(int) * ((size_t)nnets + 1));
    for (i = 0; i < nnets; i++) drv[i] = -1;
    for (j = 0; j < N; j++) if (!nodes[j].dead) for (i = 0; i < nodes[j].nout; i++) drv[find(nodes[j].out[i])] = j;

    /* edges driver -> reader */
    for (j = 0; j < N; j++) if (!nodes[j].dead) ne += nodes[j].nin;
    ea = (int *)malloc(sizeof(int) * ((size_t)ne + 1));
    eb = (int *)malloc(sizeof(int) * ((size_t)ne + 1));
    ne = 0;
    for (j = 0; j < N; j++) {
        if (nodes[j].dead) continue;
        for (i = 0; i < nodes[j].nin; i++) {
            int d = drv[find(nodes[j].in[i])];
            if (d >= 0) { ea[ne] = d; eb[ne] = j; ne++; }
        }
    }
    outdeg = (int *)calloc((size_t)N + 1, sizeof(int));
    outstart = (int *)calloc((size_t)N + 2, sizeof(int));
    outlist = (int *)malloc(sizeof(int) * ((size_t)ne + 1));
    for (e = 0; e < ne; e++) outdeg[ea[e]]++;
    for (j = 0; j < N; j++) outstart[j + 1] = outstart[j] + outdeg[j];
    for (j = 0; j < N; j++) outdeg[j] = 0;
    for (e = 0; e < ne; e++) outlist[outstart[ea[e]] + outdeg[ea[e]]++] = e;

    /* classicOrder */
    back = (char *)calloc((size_t)ne + 1, 1);
    state = (int *)calloc((size_t)N + 1, sizeof(int));
    work = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    wpos = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    {
        int pass, s;
        for (pass = 0; pass < 3; pass++)
            for (s = 0; s < N; s++) {
                int k = nodes[s].kind;
                if (nodes[s].dead || state[s]) continue;
                if (pass == 0 && !(is_source(k) || k == K_PIN_IN)) continue;
                if (pass == 1 && k != K_CONST) continue;
                wp = 0; work[wp] = s; wpos[wp++] = 0; state[s] = 1;
                while (wp) {
                    int u = work[wp - 1];
                    if (wpos[wp - 1] < outdeg[u]) {
                        int ei = outlist[outstart[u] + wpos[wp - 1]++], v = eb[ei];
                        if (state[v] == 1) back[ei] = 1;
                        else if (state[v] == 0) { state[v] = 1; work[wp] = v; wpos[wp++] = 0; }
                    } else { state[u] = 2; wp--; }
                }
            }
    }
    indeg = (int *)calloc((size_t)N + 1, sizeof(int));
    rank = (int *)calloc((size_t)N + 1, sizeof(int));
    q = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    for (e = 0; e < ne; e++) if (!back[e]) indeg[eb[e]]++;
    for (j = 0; j < N; j++) if (!nodes[j].dead && !indeg[j]) q[qt++] = j;
    while (qh < qt) {
        int u = q[qh++];
        rank[u] = pos++;
        for (i = 0; i < outdeg[u]; i++) {
            int ei = outlist[outstart[u] + i];
            if (!back[ei] && --indeg[eb[ei]] == 0) q[qt++] = eb[ei];
        }
    }

    /* strongly connected components over the logic between flip-flops (a
     * flip-flop's outputs depend at once only on rst, en and down) */
    idx = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    low = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    stk = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    onstk = (int *)calloc((size_t)N + 1, sizeof(int));
    comp_of = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    order = (int *)malloc(sizeof(int) * ((size_t)N + 1));
    for (j = 0; j < N; j++) idx[j] = -1;
    {
        /* combinational successors: edges whose reader uses that input at once */
        int *cdeg = (int *)calloc((size_t)N + 1, sizeof(int)), *cstart = (int *)calloc((size_t)N + 2, sizeof(int));
        int *clist = (int *)malloc(sizeof(int) * ((size_t)ne + 1)), r;
        char *selfloop = (char *)calloc((size_t)N + 1, 1);
        char *comb = (char *)calloc((size_t)ne + 1, 1);
        for (e = 0; e < ne; e++) if ((comb[e] = (char)comb_edge(&nodes[eb[e]], ea[e], drv))) cdeg[ea[e]]++;
        for (j = 0; j < N; j++) cstart[j + 1] = cstart[j] + cdeg[j];
        for (j = 0; j < N; j++) cdeg[j] = 0;
        for (e = 0; e < ne; e++) {
            if (!comb[e]) continue;
            clist[cstart[ea[e]] + cdeg[ea[e]]++] = eb[e];
            if (ea[e] == eb[e]) selfloop[ea[e]] = 1;
        }
        /* Tarjan, iterative */
        for (r = 0; r < N; r++) {
            if (nodes[r].dead || idx[r] >= 0) continue;
            wp = 0; work[wp] = r; wpos[wp++] = 0;
            idx[r] = low[r] = counter++; stk[sp++] = r; onstk[r] = 1;
            while (wp) {
                int u = work[wp - 1];
                if (wpos[wp - 1] < cdeg[u]) {
                    int v = clist[cstart[u] + wpos[wp - 1]++];
                    if (idx[v] < 0) { idx[v] = low[v] = counter++; stk[sp++] = v; onstk[v] = 1; work[wp] = v; wpos[wp++] = 0; }
                    else if (onstk[v] && idx[v] < low[u]) low[u] = idx[v];
                } else {
                    wp--;
                    if (wp) { int p = work[wp - 1]; if (low[u] < low[p]) low[p] = low[u]; }
                    if (low[u] == idx[u]) {
                        int v;
                        do { v = stk[--sp]; onstk[v] = 0; comp_of[v] = ncomp; order[norder++] = v; } while (v != u);
                        ncomp++;
                    }
                }
            }
        }
        /* components come out sinks first, each one's members together in
         * order[]: run them from the last found to the first; inside a loop,
         * by the classic rank */
        {
            int a = norder;
            int *members = (int *)malloc(sizeof(int) * ((size_t)N + 1));
            while (a > 0) {
                int c0 = comp_of[order[a - 1]], b = a, m = 0, it, x, y;
                while (b > 0 && comp_of[order[b - 1]] == c0) b--;
                for (x = b; x < a; x++) members[m++] = order[x];
                for (x = 1; x < m; x++) {           /* sort by rank */
                    int t = members[x];
                    for (y = x; y > 0 && rank[members[y - 1]] > rank[t]; y--) members[y] = members[y - 1];
                    members[y] = t;
                }
                if (m == 1 && !selfloop[members[0]]) sim_node(members[0], NULL);
                else {
                    for (it = 0; it < 200; it++) {
                        int changed = 0;
                        for (x = 0; x < m; x++) sim_node(members[x], &changed);
                        if (!changed) break;
                    }
                    for (x = 0; x < m; x++) {
                        Node *mn = &nodes[members[x]];
                        for (y = 0; y < mn->nout; y++) in_loop[find(mn->out[y])] = 1;
                    }
                }
                a = b;
            }
            free(members);
        }
        free(cdeg); free(cstart); free(clist); free(selfloop); free(comb);
    }
    free(drv); free(ea); free(eb); free(outdeg); free(outstart); free(outlist); free(back); free(state);
    free(work); free(wpos); free(indeg); free(rank); free(q); free(idx); free(low); free(stk); free(onstk);
    free(comp_of); free(order);
}

/* ---- one entity ---------------------------------------------------------- */

typedef struct { int comp; char *entity; char **ports; int nports; } VComp;
static VComp *vcomps;
static int nvcomps, capvcomps;
static char *vtop_entity;
static VBuf vout;
static VBuf vrenamed;          /* "a=b " pairs: Wire names renamed for VHDL */

static int vcomp_find(int comp)
{
    int i;
    for (i = 0; i < nvcomps; i++) if (vcomps[i].comp == comp) return i;
    return -1;
}

/* the ports of an instance, in node order: pins (inst > 0) or the circuit's
 * sources and sinks (inst 0) */
static int is_port_node(const Node *n, int inst)
{
    if (n->dead || n->inst != inst) return 0;
    if (inst == 0) return is_source(n->kind) || is_sink(n->kind);
    return is_pin(n->kind);
}

static void vsens_add(VBuf *b, int net, int *first)
{
    if (net < 0 || vis_const(net)) return;
    vbf(b, "%s%s", *first ? "" : ", ", N(net));
    *first = 0;
}

/* a statement assigning an unsigned value to a net: std_logic_vector(x),
 * or x(0) for a 1-bit net */
static void vassign_u(VBuf *b, const char *ind, int net, const char *expr)
{
    if (net_w(net) == 1) vbf(b, "%s%s <= %s(0);\n", ind, N(net), expr);
    else vbf(b, "%s%s <= std_logic_vector(%s);\n", ind, N(net), expr);
}

/* an input of a mul, as a signed value one bit wider: its sign (or a 0) on top */
static const char *mul_operand(int net, int w, int sgn, char *buf, size_t cap)
{
    const char *x;
    net = find(net);
    x = N(net);
    if (!sgn) snprintf(buf, cap, w == 1 || vis_const(net) ? "signed'('0' & %s)" : "signed('0' & %s)", x);
    else if (w == 1) snprintf(buf, cap, "signed'(%s & %s)", x, x);
    else snprintf(buf, cap, vis_const(net) ? "resize(signed'(%s), %d)" : "resize(signed(%s), %d)", x, w + 1);
    return buf;
}

static void emit_node(VBuf *b, VBuf *decl, int j)
{
    Node *n = &nodes[j];
    char t1[512], t2[512], t3[512], lit[80], lit2[80];
    int i, o = n->nout ? find(n->out[0]) : -1, w = o >= 0 ? net_w(o) : 1;
    const char *op = NULL;

    switch (n->kind) {
    case K_CONST: case K_INPUT: case K_BUTTON: case K_CLOCK: case K_KEYPAD:
        return;                                 /* literals and ports */
    case K_AND: op = "and"; goto gate;
    case K_NAND: op = "and"; goto gate;
    case K_OR: op = "or"; goto gate;
    case K_NOR: op = "or"; goto gate;
    case K_XOR: op = "xor"; goto gate;
    case K_XNOR: op = "xor";
    gate: {
        int neg = n->kind == K_NAND || n->kind == K_NOR || n->kind == K_XNOR;
        vbf(b, "  %s <= %s", N(o), neg ? "not (" : "");
        for (i = 0; i < n->nin; i++) {
            if (i) vbf(b, " %s ", op);
            vbf(b, "%s", N(n->in[i]));
        }
        vbf(b, "%s;\n", neg ? ")" : "");
        return;
    }
    case K_NOT:
        vbf(b, "  %s <= not %s;\n", N(o), N(n->in[0]));
        return;
    case K_BUF:
        vbf(b, "  %s <= %s;\n", N(o), N(n->in[0]));
        return;
    case K_TRI:
        vbf(b, "  %s <= %s when %s else %s;\n", N(o), N(n->in[0]), C(n->in[1], 1),
            w == 1 ? "'Z'" : "(others => 'Z')");
        return;
    case K_BUS:
        for (i = 0; i < n->nin; i++) vbf(b, "  %s <= %s;%s\n", N(o), N(n->in[i]), i ? "" : "   -- one driver per tri(); 'Z' when it is not talking");
        return;
    case K_MUX: {
        int sel = find(n->in[n->nin - 1]), sw = net_w(sel), nd = n->nin - 1;
        vbf(b, "  with %s select %s <=\n", N(sel), N(o));
        for (i = 0; i < nd; i++) {
            if (i < nd - 1) vbf(b, "    %s when %s,\n", N(n->in[i]), vlit((unsigned long)i, sw, lit));
            else vbf(b, "    %s when others;\n", N(n->in[i]));
        }
        return;
    }
    case K_DEMUX: {
        int sel = find(n->in[1]), sw = net_w(sel);
        for (i = 0; i < n->nout; i++) {
            int on = find(n->out[i]);
            if (!vreaders[on] && i) continue;
            vbf(b, "  %s <= %s when %s = %s else %s;\n", N(on), N(n->in[0]), N(sel), vlit((unsigned long)i, sw, lit),
                net_w(on) == 1 ? "'0'" : "(others => '0')");
        }
        return;
    }
    case K_DECODER: {
        int sel = find(n->in[0]), en = vin_by(n, "en");
        vbf(b, "  process (");
        { int f = 1; vsens_add(b, sel, &f); vsens_add(b, en, &f); if (f) vbf(b, "%s", N(sel)); }
        vbf(b, ")\n  begin\n    %s <= (others => '0');\n", N(o));
        if (en >= 0) vbf(b, "    if %s then\n      %s(to_integer(%s)) <= '1';\n    end if;\n", C(en, 1), N(o), U(sel, t1, sizeof t1));
        else vbf(b, "    %s(to_integer(%s)) <= '1';\n", N(o), U(sel, t1, sizeof t1));
        vbf(b, "  end process;\n");
        return;
    }
    case K_ENCODER: {
        int a = find(n->in[0]), aw = net_w(a), v1 = find(n->out[1]), f = 1;
        vbf(b, "  process (");
        vsens_add(b, a, &f);
        if (f) vbf(b, "%s", N(a));
        char idx[200], k[200];
        snprintf(idx, sizeof idx, "%s", vname_take("idx", NULL));
        snprintf(k, sizeof k, "%s", vname_take("k", NULL));
        vbf(b, ")\n    variable %s : unsigned(%d downto 0);\n  begin\n    %s := (others => '0');\n", idx, w - 1, idx);
        if (vreaders[v1]) vbf(b, "    %s <= '0';\n", N(v1));
        vbf(b, "    for %s in 0 to %d loop          -- the highest 1 wins\n      if %s(%s) = '1' then\n        %s := to_unsigned(%s, %d);\n",
            k, aw - 1, N(a), k, idx, k, w);
        if (vreaders[v1]) vbf(b, "        %s <= '1';\n", N(v1));
        vbf(b, "      end if;\n    end loop;\n");
        vassign_u(b, "    ", o, idx);
        vbf(b, "  end process;\n");
        return;
    }
    case K_ADD: {
        int a = find(n->in[0]), bb = find(n->in[1]), c = vin_by(n, "cin"), co = find(n->out[1]), f = 1;
        char sum[300], var[200];
        snprintf(var, sizeof var, "%s", vname_take("sum", NULL));   /* not the name of any signal here */
        vbf(b, "  process (");
        vsens_add(b, a, &f); vsens_add(b, bb, &f); vsens_add(b, c, &f);
        if (f) vbf(b, "%s", N(a));
        vbf(b, ")\n    variable %s : unsigned(%d downto 0);\n  begin\n", var, w);
        vbf(b, "    %s := resize(%s, %d) + %s", var, U(a, t1, sizeof t1), w + 1, U(bb, t2, sizeof t2));
        if (c >= 0) vbf(b, " + %s", U(c, t3, sizeof t3));
        vbf(b, ";\n");
        snprintf(sum, sizeof sum, "%s(%d downto 0)", var, w - 1);
        vassign_u(b, "    ", o, sum);
        if (vreaders[co]) vbf(b, "    %s <= %s(%d);\n", N(co), var, w);
        vbf(b, "  end process;\n");
        return;
    }
    case K_MUL: {
        /* both inputs one bit wider, as signed: then every mix of signed and
         * unsigned is one signed multiplication, 2w + 2 bits */
        int a = find(n->in[0]), bb = find(n->in[1]), hi = find(n->out[1]), f = 1;
        char var[200];
        snprintf(var, sizeof var, "%s", vname_take("prod", NULL));
        vbf(b, "  process (");
        vsens_add(b, a, &f); vsens_add(b, bb, &f);
        if (f) vbf(b, "%s", N(a));
        vbf(b, ")\n    variable %s : signed(%d downto 0);\n  begin\n", var, 2 * w + 1);
        vbf(b, "    %s := %s * %s;\n", var, mul_operand(a, w, n->sgn & 1, t1, sizeof t1), mul_operand(bb, w, n->sgn & 2, t2, sizeof t2));
        if (w == 1) vbf(b, "    %s <= %s(0);\n", N(o), var);
        else vbf(b, "    %s <= std_logic_vector(%s(%d downto 0));\n", N(o), var, w - 1);
        if (vreaders[hi]) {
            if (w == 1) vbf(b, "    %s <= %s(1);\n", N(hi), var);
            else vbf(b, "    %s <= std_logic_vector(%s(%d downto %d));\n", N(hi), var, 2 * w - 1, w);
        }
        vbf(b, "  end process;\n");
        return;
    }
    case K_COMPARE: {
        static const char *rel[3] = { "<", "=", ">" };
        for (i = 0; i < 3; i++) {
            int on = find(n->out[i]);
            if (!vreaders[on]) continue;
            vbf(b, "  %s <= '1' when %s %s %s else '0';\n", N(on), U(n->in[0], t1, sizeof t1), rel[i], U(n->in[1], t2, sizeof t2));
        }
        return;
    }
    case K_DFF: case K_TFF: case K_JKFF: case K_COUNTER: {
        int clk = vin_by(n, "clk"), rst = vin_by(n, "rst"), en = vin_by(n, "en");
        int load = vin_by(n, "load"), d = vin_by(n, "d"), dn = vin_by(n, "down");
        int o2 = n->nout > 1 ? find(n->out[1]) : -1, f = 1;
        const char *zero = w == 1 ? "'0'" : "(others => '0')";
        const char *ind = "      ";
        int lifted = n->kind == K_COUNTER && load >= 0;
        vbf(b, "  process (");
        vsens_add(b, clk, &f); vsens_add(b, rst, &f);
        if (f) vbf(b, "%s", N(clk));
        vbf(b, ")\n  begin\n");
        if (rst >= 0) vbf(b, "    if %s then\n      %s <= %s;\n    elsif rising_edge(%s) then\n", C(rst, 1), N(o), zero, N(clk));
        else vbf(b, "    if rising_edge(%s) then\n", N(clk));
        /* load (counter) wins and needs no en; then en; then the part's own step */
        if (lifted) {
            vbf(b, "      if %s then\n        %s <= %s;\n", C(load, 1), N(o), K01(d));
            if (en >= 0) vbf(b, "      elsif %s then\n", C(en, 1));
            else vbf(b, "      else\n");
            ind = "        ";
        } else if (en >= 0) {
            vbf(b, "      if %s then\n", C(en, 1));
            ind = "        ";
        }
        switch (n->kind) {
        case K_DFF:
            vbf(b, "%s%s <= %s;\n", ind, N(o), K01(n->in[0]));
            break;
        case K_TFF:
            vbf(b, "%sif %s then\n%s  %s <= not %s;\n%send if;\n", ind, C(vin_by(n, "t"), 1), ind, N(o), N(o), ind);
            break;
        case K_JKFF: {
            int jj = vin_by(n, "j"), kk = vin_by(n, "k");
            vbf(b, "%sif %s and %s then\n%s  %s <= not %s;\n", ind, C(jj, 1), C(kk, 1), ind, N(o), N(o));
            vbf(b, "%selsif %s then\n%s  %s <= '1';\n", ind, C(jj, 1), ind, N(o));
            vbf(b, "%selsif %s then\n%s  %s <= '0';\n%send if;\n", ind, C(kk, 1), ind, N(o), ind);
            break;
        }
        case K_COUNTER:
            if (w == 1) {
                vbf(b, "%s%s <= not %s;\n", ind, N(o), N(o));
            } else if (dn >= 0) {
                vbf(b, "%sif %s then\n%s  %s <= std_logic_vector(unsigned(%s) - 1);\n", ind, C(dn, 1), ind, N(o), N(o));
                vbf(b, "%selse\n%s  %s <= std_logic_vector(unsigned(%s) + 1);\n%send if;\n", ind, ind, N(o), N(o), ind);
            } else vbf(b, "%s%s <= std_logic_vector(unsigned(%s) + 1);\n", ind, N(o), N(o));
            break;
        }
        if (lifted || en >= 0) vbf(b, "      end if;\n");
        vbf(b, "    end if;\n  end process;\n");
        if (o2 >= 0 && vreaders[o2]) {
            if (n->kind == K_COUNTER) {
                char ones[80], zeros[80];
                vlit(wmask(w), w, ones);
                vlit(0, w, zeros);
                vbf(b, "  %s <= '1' when ", N(o2));
                if (en >= 0) vbf(b, "%s and ", C(en, 1));
                if (dn >= 0) vbf(b, "((%s and %s = %s) or (%s and %s = %s))", C(dn, 1), N(o), zeros, C(dn, 0), N(o), ones);
                else vbf(b, "%s = %s", N(o), ones);
                vbf(b, " else '0';\n");
            } else vbf(b, "  %s <= not %s;\n", N(o2), N(o));
        }
        return;
    }
    case K_ROM: case K_RAM: {
        int a = vin_by(n, "addr"), words = 1 << n->memabits, f = 1;
        long k;
        char tname[200], mname[200], base[200];
        int any = 0;
        if (n->name) vhdl_ident(n->name, n->nlen, base, sizeof base);
        else snprintf(base, sizeof base, "%s%d", n->kind == K_ROM ? "rom" : "ram", ++vrams);
        snprintf(t1, sizeof t1, "%s_t", base);
        snprintf(tname, sizeof tname, "%s", vname_take(t1, NULL));
        snprintf(t1, sizeof t1, "%s_words", base);
        snprintf(mname, sizeof mname, "%s", vname_take(t1, NULL));
        if (cpu_stmt && n->inst == 0 && n->name) {   /* a part the cpu statement names: remember its VHDL name */
            gram_node *tv = cpu_opt("text"), *dv = cpu_opt("data"), *l;
            int idx = 0;
            if (tv && same(tv, n->name, n->nlen)) vcpu_text = vdup(N(o));
            if (dv)
                for (l = dv->rule == WIRE_R_list ? dv->child : dv; l && idx < 64; l = dv->rule == WIRE_R_list ? l->next : NULL, idx++)
                    if (same(l, n->name, n->nlen)) vcpu_lane[idx] = vdup(mname);
        }
        if (n->screenw)
            vbf(decl, "  -- the pixels of a %d x %d screen, address {y, x}: on a board, a video\n"
                      "  -- circuit (VGA) would read them out; wire_screen is how Wire knows\n", n->screenw, words / n->screenw);
        vbf(decl, "  type %s is array (0 to %d) of std_logic_vector(%d downto 0);\n", tname, words - 1, n->membits - 1);
        vbf(decl, "  %s %s : %s := (", n->kind == K_ROM ? "constant" : "signal", mname, tname);
        for (k = 0; k < n->memlen && k < words; k++) {
            if (!n->mem[k]) continue;
            vbf(decl, "%s\n    %ld => ", any ? "," : "", k);
            if (n->membits == 1) vbf(decl, "\"%lu\"", n->mem[k] & 1);
            else vbf(decl, "%s", vlit(n->mem[k], n->membits, lit2));
            any = 1;
        }
        vbf(decl, "%s%sothers => (others => '0'));\n", any ? "," : "", any ? "\n    " : "");
        if (n->screenw) {
            if (!vscreens++) vbf(decl, "  attribute wire_screen : integer;\n");
            vbf(decl, "  attribute wire_screen of %s : signal is %d;\n", mname, n->screenw);
            if (n->screentext) {   /* its words are characters */
                if (!vtexts++) vbf(decl, "  attribute wire_text : integer;\n");
                vbf(decl, "  attribute wire_text of %s : signal is 1;\n", mname);
            }
        }
        if (n->kind == K_RAM) {
            int clk = vin_by(n, "clk"), we = vin_by(n, "we"), din = vin_by(n, "din");
            vbf(b, "  process (%s)\n  begin\n    if rising_edge(%s) then\n      if %s then\n", N(clk), N(clk), C(we, 1));
            if (n->membits == 1) vbf(b, "        %s(to_integer(%s))(0) <= %s;\n", mname, U(a, t1, sizeof t1), K01(din));
            else vbf(b, "        %s(to_integer(%s)) <= %s;\n", mname, U(a, t1, sizeof t1), K01(din));
            vbf(b, "      end if;\n    end if;\n  end process;\n");
        }
        (void)f;
        if (w == 1) vbf(b, "  %s <= %s(to_integer(%s))(0);\n", N(o), mname, U(a, t1, sizeof t1));
        else vbf(b, "  %s <= %s(to_integer(%s));\n", N(o), mname, U(a, t1, sizeof t1));
        return;
    }
    case K_SPLIT: {
        int a = find(n->in[0]), aw = net_w(a);
        for (i = 0; i < n->nout; i++) {
            int on = find(n->out[i]), hi = n->range[i][0], lo = n->range[i][1];
            if (!vn[on]) continue;   /* a piece nothing reads has no signal */
            if (vis_const(a)) {
                unsigned long v = nodes[vdriver[a]].value >> lo;
                vbf(b, "  %s <= %s;\n", N(on), vlit(v & wmask(hi - lo + 1), hi - lo + 1, lit));
            } else if (aw == 1) vbf(b, "  %s <= %s;\n", N(on), N(a));
            else if (hi == lo) vbf(b, "  %s <= %s(%d);\n", N(on), N(a), lo);
            else vbf(b, "  %s <= %s(%d downto %d);\n", N(on), N(a), hi, lo);
        }
        return;
    }
    case K_MERGE: {
        /* one concatenation when the pieces tile the bus, top first */
        int order[MAXIO], m = n->nin, x, y, tiles = 1, top = w - 1;
        for (x = 0; x < m; x++) order[x] = x;
        for (x = 1; x < m; x++) {
            int tt = order[x];
            for (y = x; y > 0 && n->range[order[y - 1]][0] < n->range[tt][0]; y--) order[y] = order[y - 1];
            order[y] = tt;
        }
        for (x = 0; x < m; x++) {
            if (n->range[order[x]][0] != top) tiles = 0;
            top = n->range[order[x]][1] - 1;
        }
        if (top != -1) tiles = 0;
        if (w == 1) { vbf(b, "  %s <= %s;\n", N(o), N(n->in[0])); return; }
        if (tiles && m > 1) {
            vbf(b, "  %s <= ", N(o));
            for (x = 0; x < m; x++) vbf(b, "%s%s", x ? " & " : "", N(n->in[order[x]]));
            vbf(b, ";\n");
        } else {
            for (x = 0; x < m; x++) {
                int hi = n->range[x][0], lo = n->range[x][1];
                if (hi == lo) vbf(b, "  %s(%d) <= %s;\n", N(o), hi, N(n->in[x]));
                else vbf(b, "  %s(%d downto %d) <= %s;\n", N(o), hi, lo, N(n->in[x]));
            }
        }
        return;
    }
    default:
        return;
    }
}

/* the preferred name of a net with no name of its own */
static void vnet_want(int net, char *buf, size_t cap)
{
    Net *nt = &nets[find(net)];
    int d = vdriver[find(net)];
    if (nt->name && nt->nlen) { vhdl_ident(nt->name, nt->nlen, buf, cap); return; }
    if (d >= 0 && nodes[d].name && nodes[d].nout > 1) {
        char a[128], o[64];
        int i;
        vhdl_ident(nodes[d].name, nodes[d].nlen, a, sizeof a);
        for (i = 0; i < nodes[d].nout; i++) if (find(nodes[d].out[i]) == find(net)) break;
        vhdl_ident(nodes[d].outname[i < nodes[d].nout ? i : 0], (int)strlen(nodes[d].outname[i < nodes[d].nout ? i : 0]), o, sizeof o);
        snprintf(buf, cap, "%s_%s", a, o);
        return;
    }
    snprintf(buf, cap, "n%d", ++vnamed);
}

static void vscope_reset(void)
{
    int i;
    for (i = 0; i < nnets; i++) {
        if (vn[i]) free(vn[i]);
        vn[i] = NULL; vreaders[i] = 0; vdriver[i] = -1; vsignal[i] = 0; vportnet[i] = 0;
    }
    for (i = 0; i < nvtaken; i++) free(vtaken[i]);
    nvtaken = 0;
    vnamed = 0;
    vk01[0] = 0;
    vrams = 0;
    vscreens = 0;
    vtexts = 0;
}

static void vdeclare(VBuf *decl, int net)
{
    char ty[64], lit[80];
    net = find(net);
    if (vsignal[net] || vportnet[net] || vis_const(net) || !vn[net]) return;
    vsignal[net] = 1;
    vbf(decl, "  signal %s : %s", vn[net], vtype(net_w(net), ty, sizeof ty));
    if (vhasinit[net]) vbf(decl, " := %s", vlit(vinitv[net], net_w(net), lit));
    vbf(decl, ";\n");
}

/* write the entity for instance `inst` (0 = the circuit) */
static void vemit_entity(int inst, const char *ename)
{
    VBuf body = { 0 }, decl = { 0 }, ports = { 0 };
    int i, j, np = 0, nports = 0;
    char buf[256], ty[64];
    char **pnames = NULL;
    int *pnode = NULL, capp = 0;

    vscope_reset();
    vname_take(ename, NULL);      /* a port or signal named like the entity would hide it */
    for (j = 0; j < nnodes; j++) if (is_port_node(&nodes[j], inst)) nports++;
    pnames = (char **)malloc(sizeof(char *) * ((size_t)nports + 1));
    pnode = (int *)malloc(sizeof(int) * ((size_t)nports + 1));
    (void)capp;

    /* drivers and readers in this entity */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        if (n->dead) continue;
        if (n->inst == inst && !is_pin(n->kind)) {
            for (i = 0; i < n->nin; i++) vreaders[find(n->in[i])]++;
            for (i = 0; i < n->nout; i++) vdriver[find(n->out[i])] = j;
        } else if (n->inst == inst && n->kind == K_PIN_OUT) {
            vreaders[find(n->in[0])]++;           /* the net inside, read by the port */
        } else if (n->inst == inst && n->kind == K_PIN_IN) {
            vdriver[find(n->out[0])] = j;         /* the net inside, driven by the port */
        } else if (n->inst > 0 && insts[n->inst].parent == inst && is_pin(n->kind)) {
            if (n->kind == K_PIN_IN) vreaders[find(n->in[0])]++;
            else vdriver[find(n->out[0])] = j;
        }
    }

    /* ports, in node order */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        char want[128];
        if (!is_port_node(n, inst)) continue;
        vhdl_ident(n->name ? n->name : "p", n->name ? n->nlen : 1, want, sizeof want);
        pnames[np] = vdup(vname_take(want, NULL));
        pnode[np] = j;
        if (inst == 0 && (n->nlen != (int)strlen(pnames[np]) || strncmp(n->name, pnames[np], (size_t)n->nlen)))
            vbf(&vrenamed, "%s%.*s=%s", vrenamed.n ? " " : "", n->nlen, n->name, pnames[np]);
        np++;
    }

    /* constants are literals */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        char lit[80];
        if (n->dead || n->inst != inst || n->kind != K_CONST) continue;
        vn[find(n->out[0])] = vdup(vlit(n->value, net_w(n->out[0]), lit));
    }
    /* ports that can stand for a net: inputs always; an output when only it
     * reads the net (VHDL-93 cannot read an out port) */
    for (i = 0; i < np; i++) {
        Node *n = &nodes[pnode[i]];
        int net, d;
        if (is_source(n->kind) || n->kind == K_PIN_IN) {
            net = find(n->out[0]);
            if (!vn[net]) { vn[net] = vdup(pnames[i]); vportnet[net] = 1; }
            continue;
        }
        net = find(n->in[0]);
        d = vdriver[net];
        if (vn[net] || vreaders[net] != 1) continue;
        if (d >= 0 && (is_seq(nodes[d].kind) || nodes[d].kind == K_PIN_IN || is_source(nodes[d].kind))) continue;
        vn[net] = vdup(pnames[i]);
        vportnet[net] = 1;
    }
    /* the child instances' labels, before the signals: a label keeps its name
     * and a signal named after it gets the _i, so a round trip through VHDL
     * does not add another _u to every chip's name */
    {
        static char **labs;
        static int caplabs;
        labs = (char **)grow(labs, &caplabs, ninsts + 1, sizeof *labs);
        for (i = 1; i < ninsts; i++) {
            int vc;
            labs[i] = NULL;
            if (insts[i].parent != inst) continue;
            vc = vcomp_find(insts[i].comp);
            if (insts[i].name) vhdl_ident(insts[i].name, insts[i].nlen, buf, sizeof buf);
            else snprintf(buf, sizeof buf, "u_%s", vcomps[vc].entity);
            labs[i] = vname_take(buf, "u");
        }
        vlabs = labs;
    }
    /* every other net here gets a signal */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        int touches = n->inst == inst || (n->inst > 0 && insts[n->inst].parent == inst && is_pin(n->kind));
        if (n->dead || !touches) continue;
        for (i = 0; i < n->nin + n->nout; i++) {
            int net = find(i < n->nin ? n->in[i] : n->out[i - n->nin]);
            char want[200];
            if (n->inst != inst && is_pin(n->kind)) {
                /* a child's pin: only the net outside it is ours */
                if ((n->kind == K_PIN_IN) != (i < n->nin)) continue;
            }
            if (n->inst == inst && n->kind == K_PIN_IN && i < n->nin) continue;   /* outside of us */
            if (n->inst == inst && n->kind == K_PIN_OUT && i >= n->nin) continue;
            if (vn[net]) continue;
            /* an output of a part that nothing reads (a flip-flop's nq) is left out */
            if (!vreaders[net] && vdriver[net] >= 0 && find(nodes[vdriver[net]].out[0]) != net) continue;
            vnet_want(net, want, sizeof want);
            vn[net] = vdup(vname_take(want, "i"));
        }
    }

    /* the statements */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        if (n->dead || n->inst != inst || is_pin(n->kind)) continue;
        emit_node(&body, &decl, j);
    }
    /* child instances */
    for (i = 1; i < ninsts; i++) {
        int vc, first = 1;
        char lab[160];
        if (insts[i].parent != inst) continue;
        vc = vcomp_find(insts[i].comp);
        snprintf(lab, sizeof lab, "%s", vlabs[i]);
        vbf(&body, "  %s: entity work.%s port map (", lab, vcomps[vc].entity);
        {
            int k = 0;
            for (j = 0; j < nnodes; j++) {
                Node *n = &nodes[j];
                int net;
                if (!is_port_node(n, i)) continue;
                net = n->kind == K_PIN_IN ? find(n->in[0]) : find(n->out[0]);
                vbf(&body, "%s%s => %s", first ? "" : ", ", vcomps[vc].ports[k], N(net));
                first = 0;
                k++;
            }
        }
        vbf(&body, ");\n");
    }
    /* outputs that read a signal */
    for (i = 0; i < np; i++) {
        Node *n = &nodes[pnode[i]];
        int net;
        if (is_source(n->kind) || n->kind == K_PIN_IN) continue;
        net = find(n->in[0]);
        if (vportnet[net] && !strcmp(vn[net], pnames[i])) continue;
        vbf(&body, "  %s <= %s;\n", pnames[i], N(net));
    }

    /* signal declarations: every named net that is not a port or literal */
    for (j = 0; j < nnodes; j++) {
        Node *n = &nodes[j];
        int touches = n->inst == inst || (n->inst > 0 && insts[n->inst].parent == inst && is_pin(n->kind));
        if (n->dead || !touches) continue;
        for (i = 0; i < n->nin + n->nout; i++) {
            int net = find(i < n->nin ? n->in[i] : n->out[i - n->nin]);
            if (n->inst != inst && is_pin(n->kind) && (n->kind == K_PIN_IN) != (i < n->nin)) continue;
            if (n->inst == inst && n->kind == K_PIN_IN && i < n->nin) continue;
            if (n->inst == inst && n->kind == K_PIN_OUT && i >= n->nin) continue;
            vdeclare(&decl, net);
        }
    }

    if (vk01[0]) {
        vbf(&decl, "  -- Wire stores a bit that nothing drives ('Z'), or that is unknown, as 0\n");
        vbf(&decl, "  function %s(x : std_logic) return std_logic is\n  begin\n", vk01);
        vbf(&decl, "    if x = '1' then return '1'; else return '0'; end if;\n  end %s;\n", vk01);
        vbf(&decl, "  function %s(x : std_logic_vector) return std_logic_vector is\n", vk01);
        vbf(&decl, "    variable r : std_logic_vector(x'range);\n  begin\n");
        vbf(&decl, "    for i in x'range loop\n      r(i) := %s(x(i));\n    end loop;\n    return r;\n  end %s;\n", vk01, vk01);
    }

    /* the entity */
    vbf(&ports, "entity %s is\n", ename);
    if (np) {
        int wid = 0;
        for (i = 0; i < np; i++) if ((int)strlen(pnames[i]) > wid) wid = (int)strlen(pnames[i]);
        vbf(&ports, "  port (\n");
        for (i = 0; i < np; i++) {
            Node *n = &nodes[pnode[i]];
            int in = is_source(n->kind) || n->kind == K_PIN_IN;
            int net = in ? find(n->out[0]) : find(n->in[0]);
            vbf(&ports, "    %-*s : %s %s%s\n", wid, pnames[i], in ? "in " : "out", vtype(net_w(net), ty, sizeof ty), i + 1 < np ? ";" : "");
        }
        vbf(&ports, "  );\n");
    }
    vbf(&ports, "end %s;\n", ename);

    vbf(&vout, "library ieee;\nuse ieee.std_logic_1164.all;\nuse ieee.numeric_std.all;\n\n");
    if (inst == 0) vbf(&vout, "-- the circuit\n");
    else vbf(&vout, "-- component %s\n", vcomps[vcomp_find(insts[inst].comp)].entity);
    vb_add(&vout, ports.s ? ports.s : "", ports.n);
    vbf(&vout, "\narchitecture rtl of %s is\n", ename);
    if (inst == 0 && cpu_stmt) vemit_cpu_attr(&decl);
    if (decl.s) vb_add(&vout, decl.s, decl.n);
    vbf(&vout, "begin\n");
    if (body.s) vb_add(&vout, body.s, body.n);
    vbf(&vout, "end rtl;\n\n");

    /* remember a component's ports for its instances */
    if (inst > 0) {
        int vc = vcomp_find(insts[inst].comp);
        vcomps[vc].ports = pnames;
        vcomps[vc].nports = np;
        pnames = NULL;
    }
    free(pnames);
    free(pnode);
    free(body.s); free(decl.s); free(ports.s);
}


/* the cpu statement, as attribute wire_cpu of the architecture (wire_vhdl_in.c reads it back) */
/* the VHDL name of the wire (or output) a cpu setting names: what drives or carries it */
static const char *vcpu_wire(const gram_node *v, char *buf, size_t n)
{
    int i;
    for (i = 0; i < nnets; i++)
        if (nets[i].name && nets[i].nlen == (int)v->len && !memcmp(nets[i].name, v->text, v->len)) return N(i);
    vhdl_ident(v->text, (int)v->len, buf, n);
    return buf;
}

static void vemit_cpu_attr(VBuf *decl)
{
    gram_node *v, *l;
    VBuf s = { 0, 0, 0 };
    char id[256];
    int idx = 0;
    v = cpu_opt("isa");
    vbf(&s, "isa=%.*s", (int)v->len - 2, v->text + 1);
    v = cpu_opt("text");
    vhdl_ident(v->text, (int)v->len, id, sizeof id);
    vbf(&s, "; text=%s", vcpu_text ? vcpu_text : id);
    v = cpu_opt("data");
    if (v) {
        vbf(&s, "; data=");
        for (l = v->rule == WIRE_R_list ? v->child : v; l && idx < 64; l = v->rule == WIRE_R_list ? l->next : NULL, idx++) {
            vhdl_ident(l->text, (int)l->len, id, sizeof id);
            vbf(&s, "%s%s", idx ? ", " : "", vcpu_lane[idx] ? vcpu_lane[idx] : id);
        }
    }
    if ((v = cpu_opt("text_base"))) vbf(&s, "; text_base=%.*s", (int)v->len, v->text);
    if ((v = cpu_opt("data_base"))) vbf(&s, "; data_base=%.*s", (int)v->len, v->text);
    if ((v = cpu_opt("pc"))) vbf(&s, "; pc=%s", vcpu_wire(v, id, sizeof id));
    if ((v = cpu_opt("halt"))) vbf(&s, "; halt=%s", vcpu_wire(v, id, sizeof id));
    if ((v = cpu_opt("regs"))) vbf(&s, "; regs=%.*s", (int)v->len - 2, v->text + 1);
    vbf(decl, "  -- how the circuit is programmed: Wire's cpu statement\n"
              "  attribute wire_cpu : string;\n  attribute wire_cpu of rtl : architecture is \"%s\";\n", s.s);
    free(s.s);
}

/* components first (an entity must be read before it is used), each from
 * its first use */
static void vemit_tree(int inst)
{
    int i;
    for (i = 1; i < ninsts; i++) {
        if (insts[i].parent != inst || vcomp_find(insts[i].comp) >= 0) continue;
        vcomps = (VComp *)grow(vcomps, &capvcomps, nvcomps + 1, sizeof *vcomps);
        memset(&vcomps[nvcomps], 0, sizeof *vcomps);
        vcomps[nvcomps].comp = insts[i].comp;
        nvcomps++;
        vemit_tree(i);
    }
    if (inst > 0) {
        int vc = vcomp_find(insts[inst].comp);
        char want[200];
        vhdl_ident(comps[insts[inst].comp].name, (int)strlen(comps[insts[inst].comp].name), want, sizeof want);
        {
            /* entity names: unique among themselves and apart from the circuit's */
            char base[160];
            int k, clash, suffix = 1;
            snprintf(base, sizeof base, "%.150s", want);
            do {
                clash = 0;
                {
                    const char *a = want, *t = vtop_entity;
                    while (*a && *t && (*a | 32) == (*t | 32)) a++, t++;
                    if (!*a && !*t) clash = 1;
                }
                for (k = 0; k < nvcomps && !clash; k++) {
                    const char *a = want, *t = vcomps[k].entity;
                    if (!t) continue;
                    while (*a && *t && (*a | 32) == (*t | 32)) a++, t++;
                    if (!*a && !*t) clash = 1;
                }
                if (clash) {
                    if (suffix == 1) snprintf(want, sizeof want, "%s_c", base);
                    else snprintf(want, sizeof want, "%s_c%d", base, suffix);
                    suffix++;
                }
            } while (clash);
        }
        vcomps[vc].entity = vdup(want);
        vemit_entity(inst, vcomps[vc].entity);
    }
}

/* The VHDL of the whole circuit, as text. `top` names the circuit's entity. */
static char *vhdl_generate(const char *top, const char *fname)
{
    int i;
    char want[200];
    vn = (char **)calloc((size_t)nnets + 1, sizeof *vn);
    vreaders = (int *)calloc((size_t)nnets + 1, sizeof *vreaders);
    vdriver = (int *)calloc((size_t)nnets + 1, sizeof *vdriver);
    vsignal = (char *)calloc((size_t)nnets + 1, 1);
    vportnet = (char *)calloc((size_t)nnets + 1, 1);
    vinitv = (unsigned long *)calloc((size_t)nnets + 1, sizeof *vinitv);
    vhasinit = (char *)calloc((size_t)nnets + 1, 1);

    /* which nets can float or be unknown: from tri() and buses, through the
     * logic (flip-flops store 0 instead, see K01) */
    vmayx = (char *)calloc((size_t)nnets + 1, 1);
    for (i = 0; i < nnodes; i++)
        if (!nodes[i].dead && (nodes[i].kind == K_TRI || nodes[i].kind == K_BUS)) vmayx[find(nodes[i].out[0])] = 1;
    for (;;) {
        int grew = 0, k, any;
        for (i = 0; i < nnodes; i++) {
            Node *n = &nodes[i];
            if (n->dead || is_seq(n->kind) || is_source(n->kind)) continue;
            for (any = 0, k = 0; k < n->nin; k++) {
                if (!vmayx[find(n->in[k])]) continue;
                if (n->kind == K_RAM && strcmp(n->inname[k], "addr")) continue;   /* din is stored, see K01 */
                any = 1;
            }
            if (!any) continue;
            for (k = 0; k < n->nout; k++) if (!vmayx[find(n->out[k])]) { vmayx[find(n->out[k])] = 1; grew = 1; }
        }
        if (!grew) break;
    }

    /* initial values: flip-flops at 0, loops of gates where Wire settles */
    power_up();
    /* a loop also needs the logic feeding it to start settled, or it sees
     * 'U' for a moment and can toggle forever: mark everything upstream of a
     * loop, up to ports and flip-flops */
    for (;;) {
        int grew = 0, k;
        for (i = 0; i < nnodes; i++) {
            Node *n = &nodes[i];
            int feeds = 0;
            if (n->dead || is_seq(n->kind) || n->kind == K_RAM || is_source(n->kind)) continue;
            for (k = 0; k < n->nout; k++) if (in_loop[find(n->out[k])]) feeds = 1;
            if (!feeds) continue;
            for (k = 0; k < n->nin; k++) if (!in_loop[find(n->in[k])]) { in_loop[find(n->in[k])] = 1; grew = 1; }
        }
        if (!grew) break;
    }
    for (i = 0; i < nnets; i++) {
        if (in_loop[i] && sk[i] == wmask(net_w(i))) { vinitv[i] = sv[i]; vhasinit[i] = 1; }
    }
    for (i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->dead || !is_seq(n->kind)) continue;
        vinitv[find(n->out[0])] = 0;
        vhasinit[find(n->out[0])] = 1;
    }

    vhdl_ident(top, (int)strlen(top), want, sizeof want);
    vtop_entity = vdup(want);
    {
        int seq = 0;
        for (i = 0; i < nnodes; i++) if (!nodes[i].dead && (is_seq(nodes[i].kind) || nodes[i].kind == K_RAM)) seq = 1;
        vbf(&vout, "-- %s.vhd: made by Wire from %s\n", vtop_entity, fname);
        vbf(&vout, "--\n-- VHDL-93, so it works in Vivado as it is (and in GHDL). Each component\n");
        vbf(&vout, "-- is an entity; %s is the whole circuit.%s\n", vtop_entity,
            seq ? " Flip-flops, counters and ram" : "");
        if (seq) vbf(&vout, "-- start at 0, like in Wire, so their signals have initial values.\n");
        vbf(&vout, "\n");
    }
    vemit_tree(0);
    vemit_entity(0, vtop_entity);
    if (vrenamed.n) {
        /* Wire names that are VHDL words */
        VBuf tmp = { 0 };
        vbf(&tmp, "-- renamed: %s\n", vrenamed.s);
        /* put it at the top, after the first line */
        {
            char *nl = strchr(vout.s, '\n');
            VBuf all = { 0 };
            vb_add(&all, vout.s, (size_t)(nl - vout.s + 1));
            vb_add(&all, tmp.s, tmp.n);
            vb_add(&all, nl + 1, strlen(nl + 1));
            free(vout.s);
            vout = all;
        }
        free(tmp.s);
    }
    /* drop the blank line at the end */
    while (vout.n > 1 && vout.s[vout.n - 1] == '\n' && vout.s[vout.n - 2] == '\n') vout.s[--vout.n] = 0;
    return vout.s;
}
