/* wire_vhdl_in.c - VHDL in: reads a VHDL file and turns it into Wire, which
 * wire.c then checks, lays out and simulates like any other circuit.
 * Included by wire.c, after wire_vhdl.c (it uses its VBuf).
 *
 *   wire adder.vhd               writes adder.html
 *   wire --to-wire adder.vhd     prints the Wire it was turned into
 *
 * What it reads: entities and their architectures, with generics (their
 * default values), std_logic, std_logic_vector, unsigned and signed ports
 * and signals, integer and vector constants, and the concurrent statements:
 * assignments, when/else and with/select. Expressions: and or xor nand nor
 * xnor not, = /= < <= > >=, + - (numeric_std, or std_logic_unsigned), &,
 * indexes and slices, aggregates like (others => '0'), the conversions
 * unsigned() signed() std_logic_vector() to_unsigned() to_signed() resize(),
 * shift_left() shift_right() and sll srl sla sra rol ror by a constant.
 * Every entity but the top one becomes a Wire component (a chip), and
 * port map places it, so chips can be built from chips. The top entity is
 * the one no other entity uses (the last one, if there are several).
 * for ... generate and if ... generate are unrolled (labels and signals
 * inside get _0, _1 ... on the end). Processes are read statement by
 * statement (if, case, for with exit, variables): a clocked one (if
 * rising_edge(clk), with or without an asynchronous reset first) becomes
 * registers (dff) with the logic in front of them, any other one becomes
 * logic, and a signal it does not give a value on every path is refused
 * (it would be a latch). Enumerations (type state is (idle, run)) are
 * numbered in binary; arrays (type mem is array (0 to 15) of ...) become a
 * signal per element, read with a mux, or a rom when constant; integers
 * with a range become unsigned.
 *
 * The Wire keeps the VHDL's lines: each statement lands on the line (and
 * near the column) it came from, so every error Wire finds later, and every
 * part's line on the page, points at the right line of the VHDL. */
#include "vhdl_grammar.h"

enum { T_SL, T_BOOL, T_SLV, T_UNS, T_SGN, T_STR, T_INT, T_ENUM, T_ARR, T_INTV };
static const char *vi_tyname[] = { "std_logic", "boolean", "std_logic_vector", "unsigned", "signed",
                                   "a literal", "integer", "a state (an enumeration)", "an array", "integer" };
enum { VK_IN, VK_OUT, VK_SIG, VK_CONST, VK_GEN, VK_LABEL, VK_VAR };

typedef struct {
    const char *name;            /* as declared (points into the VHDL) */
    int nlen;
    char *wname;                 /* its name in Wire */
    int kind, ty, w;
    int left, right, down;       /* a vector's range: left downto/to right */
    long ival;                   /* an integer constant or generic */
    unsigned long bits;          /* a vector constant */
    const gram_node *at;
    gram_node *def;              /* a port's default value (:= '0'), or NULL */
    int ut;                      /* an enumeration or array: its type (vi_types index + 1) */
    int elem0;                   /* an array signal: the index of its first element's VSym */
    unsigned long *abits;        /* an array constant: each element */
    int hasinit;                 /* a signal's initial value (:= ...) is in bits */
    /* an array signal written as a ram (one clocked process: if we then
     * mem(to_integer(addr)) <= din) and only read as mem(to_integer(addr)):
     * it becomes one ram part, not a register per element */
    gram_node *ramproc, *ramaddr, *ramdin, *ramwe, *ramclk, *raminit;
    int ramneg, rambit;
} VSym;

/* a type of the architecture: an enumeration or an array */
typedef struct {
    const gram_node *id;
    int isarr;
    gram_node *lits;             /* enumeration: the first literal */
    int n, w;                    /* enumeration: how many literals, and bits */
    VSym elem;                   /* array: the element type */
    int alo, ahi;                /* array: the lowest and highest index */
} VType;

/* an entity, its architecture and its ports (as seen from outside) */
typedef struct {
    gram_node *ent, *arch;
    int used, top, done, ctx;
    char *wname;                 /* its name in Wire */
    VSym *ports, *gens;          /* gens: the generics, with their values */
    int nports, ngens;
    long *over;                  /* a copy made for a generic map: the value of each generic */
} VEnt;

/* an expression, as Wire text, with what VHDL knows about it */
typedef struct {
    char *s;
    int ty, w;
    int atom;                    /* 2: a name, call or {...} (can be sliced); 1: ~x; 0: a & b */
    int cst;                     /* the value is known: v (bits) or iv (integer) */
    unsigned long v;
    long iv;
    int cat;                     /* s is a {...} that more can be added to */
    int tid;                     /* an enumeration value: its type (vi_types index + 1) */
    char *m0, *m1, *ms;          /* s is mux(m0, m1, sel = ms) */
    char *zx;                    /* s is zx (zxw bits) with 0s put on top: resize(x, n) */
    int zxw;
    char *csum;                  /* s is {csum.cout, csum}: an adder's sum with its carry on top */
} VX;

static const char *vi_bind_at;   /* where wires made for values are written: the process (or NULL) */
static int vi_nbind;             /* how many such wires so far: v1_, v2_ ... */
static const char *vi_src;       /* the VHDL */
static size_t vi_len;
static const char *vi_file;
static VSym *vi_syms;
static int vi_nsyms, vi_cap;
static int vi_slv_num;           /* 1: std_logic_unsigned is used, 2: std_logic_signed */
/* what the use clauses before an entity make visible (in it and its architecture) */
enum { CTX_1164 = 1, CTX_NUM = 2, CTX_UNS = 4, CTX_SGN = 8 };
static int vi_ctx;
static VBuf *vi_lines;           /* the Wire, one buffer per line of the VHDL */
static int vi_nlines;
static VEnt **vi_ents;           /* the entities, then the copies made for generic maps */
static int vi_nents, vi_nbase;
static VBuf vi_extra;            /* the Wire of those copies, after the VHDL's last line */
static int vi_to_extra;          /* vi_emitp writes there, a statement a line */
static int *vi_xline, vi_nx;     /* the VHDL line each of those lines comes from */
static const long *vi_over;      /* while a copy is read: its generics' values */
static VEnt *vi_cur;             /* the entity being turned into Wire */
static char vi_suffix[128];      /* inside generate: _3 (or _3_1 nested) for names declared there */
static VType *vi_types;          /* the enumerations and arrays declared so far */
static int vi_ntypes;
static gram_node *vi_funcs[64];  /* the functions declared so far: inlined where they are called */
static int vi_nfuncs;

#define VT(n) (int)(n)->len, (n)->text

/* ---- errors ------------------------------------------------------------ */

static void vi_fail(const gram_node *at, const char *f, ...)
{
    va_list ap;
    size_t line = 0, col = 0;
    fprintf(stderr, "%s:", vi_file);
    if (at) {
        gram_locate(vi_src, vi_len, (size_t)(at->text - vi_src), &line, &col);
        fprintf(stderr, "%lu:%lu:", (unsigned long)line, (unsigned long)col);
    }
    fputs(" error: ", stderr);
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static char *vi_fmt(const char *f, ...)
{
    va_list ap;
    int n;
    char *s;
    va_start(ap, f);
    n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    s = (char *)malloc((size_t)n + 1);
    if (!s) { fputs("wire: out of memory\n", stderr); exit(1); }
    va_start(ap, f);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    return s;
}

static int vi_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int vi_is(const gram_node *n, const char *s)   /* text is s, ignoring case */
{
    size_t i;
    if (!n || strlen(s) != n->len) return 0;
    for (i = 0; i < n->len; i++) if (vi_lower(n->text[i]) != vi_lower(s[i])) return 0;
    return 1;
}

static gram_node *vi_kid(const gram_node *n, int rule)
{
    gram_node *k;
    for (k = n ? n->child : NULL; k; k = k->next) if (k->rule == rule) return k;
    return NULL;
}

/* ---- names ------------------------------------------------------------- */

static int vi_scope;              /* names declared from here on may hide earlier ones (a function's) */

static VSym *vi_find(const gram_node *id)
{
    int i, j;
    for (i = vi_nsyms - 1; i >= 0; i--) {   /* the latest first: a function's names hide the architecture's */
        VSym *s = &vi_syms[i];
        if (s->nlen != (int)id->len) continue;
        for (j = 0; j < s->nlen; j++) if (vi_lower(s->name[j]) != vi_lower(id->text[j])) break;
        if (j == s->nlen) return s;
    }
    return NULL;
}

/* words of Wire that are fine names in VHDL */
static int vi_wire_word(const char *s, int n)
{
    static const char *words[] = { "input", "output", "wire", "led", "seg7", "hex", "button", "clock",
                                   "bus", "keypad", "circuit", "import", "component", NULL };
    int i;
    for (i = 0; words[i]; i++) if ((int)strlen(words[i]) == n && !memcmp(words[i], s, (size_t)n)) return 1;
    return 0;
}

static int vi_wname_used(const char *w)
{
    int i;
    for (i = 0; i < vi_nsyms; i++) if (vi_syms[i].wname && !strcmp(vi_syms[i].wname, w)) return 1;
    return 0;
}

static VSym *vi_declare(gram_node *id, int kind)
{
    VSym *s;
    if (vi_find(id) && vi_find(id) - vi_syms >= vi_scope) vi_fail(id, "%.*s is declared twice", VT(id));
    if (vi_nsyms == vi_cap) {
        vi_cap = vi_cap ? vi_cap * 2 : 64;
        vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms);
    }
    s = &vi_syms[vi_nsyms];
    memset(s, 0, sizeof *s);
    s->name = id->text;
    s->nlen = (int)id->len;
    s->kind = kind;
    s->at = id;
    if (kind != VK_CONST && kind != VK_GEN) {   /* signals, ports and labels get a Wire name */
        char *w = vi_fmt("%.*s%s", VT(id), vi_suffix);
        while (vi_wire_word(w, (int)strlen(w)) || vi_wname_used(w)) {
            char *w2 = vi_fmt("%s_", w);
            free(w);
            w = w2;
        }
        s->wname = w;
    }
    vi_nsyms++;
    return s;
}

/* ---- output: Wire on the VHDL's lines ---------------------------------- */

static void vi_emitp(const char *at, const char *f, ...)
{
    va_list ap;
    size_t line = 1, col = 1;
    char tmp[256], *s;
    int n;
    VBuf *b;
    gram_locate(vi_src, vi_len, (size_t)(at - vi_src), &line, &col);
    if ((int)line > vi_nlines) line = (size_t)vi_nlines;
    b = &vi_lines[line];
    if (vi_to_extra) {
        b = &vi_extra;
        vb_add(b, "\n", 1);
        vi_xline = (int *)realloc(vi_xline, (size_t)(vi_nx + 1) * sizeof *vi_xline);
        vi_xline[vi_nx++] = (int)line;
    }
    else if (b->n == 0) { size_t i; for (i = 1; i < col; i++) vb_add(b, " ", 1); }
    else vb_add(b, "; ", 2);
    va_start(ap, f);
    n = vsnprintf(tmp, sizeof tmp, f, ap);
    va_end(ap);
    if (n < (int)sizeof tmp) { vb_add(b, tmp, (size_t)n); return; }
    s = (char *)malloc((size_t)n + 1);
    va_start(ap, f);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    vb_add(b, s, (size_t)n);
    free(s);
}
#define vi_emit(node, ...) vi_emitp((node)->text, __VA_ARGS__)

/* ---- constant integers (generics, ranges, indexes) --------------------- */

static int vi_vec(int ty) { return ty == T_SLV || ty == T_UNS || ty == T_SGN || ty == T_STR; }
static int vi_low(const VSym *s) { return s->down ? s->right : s->left; }
static int vi_high(const VSym *s) { return s->down ? s->left : s->right; }

static int vi_number(const gram_node *n, long *out)
{
    long v = 0, base = 10;
    size_t i;
    const char *h = memchr(n->text, '#', n->len);
    if (h) {
        base = 0;
        for (i = 0; n->text + i < h; i++) if (n->text[i] != '_') base = base * 10 + (n->text[i] - '0');
        if (base < 2 || base > 16) vi_fail(n, "a based number has a base from 2 to 16");
        for (i = (size_t)(h - n->text) + 1; i < n->len && n->text[i] != '#'; i++) {
            int c = vi_lower(n->text[i]), d;
            if (c == '_') continue;
            d = c >= 'a' ? c - 'a' + 10 : c - '0';
            if (d >= base) vi_fail(n, "%.*s has a digit too big for base %ld", VT(n), base);
            v = v * base + d;
            if (v > 0x7fffffffL) vi_fail(n, "%.*s is too big", VT(n));
        }
    } else {
        for (i = 0; i < n->len; i++) {
            if (n->text[i] == '_') continue;
            v = v * 10 + (n->text[i] - '0');
            if (v > 0x7fffffffL) vi_fail(n, "%.*s is too big", VT(n));
        }
    }
    *out = v;
    return 1;
}

static int vi_ceval(const gram_node *n, long *out);

static int vi_attr(const gram_node *id, const gram_node *at, long *out)
{
    VSym *s = vi_find(id);
    if (!s || !vi_vec(s->ty)) return 0;
    if (vi_is(at, "length")) *out = s->w;
    else if (vi_is(at, "high")) *out = vi_high(s);
    else if (vi_is(at, "low")) *out = vi_low(s);
    else if (vi_is(at, "left")) *out = s->left;
    else if (vi_is(at, "right")) *out = s->right;
    else return 0;
    return 1;
}

/* the value of an integer expression made of numbers, generics and
 * constants; 0 if it is not one */
static int vi_ceval(const gram_node *n, long *out)
{
    long a, b;
    gram_node *k;
    switch (n->rule) {
    case VHDL_R_number: return vi_number(n, out);
    case VHDL_R_ident: {
        VSym *s = vi_find(n);
        if (s && (s->kind == VK_CONST || s->kind == VK_GEN) && s->ty == T_INT) { *out = s->ival; return 1; }
        return 0;
    }
    case VHDL_R_neg:
        if (!vi_ceval(n->child, &a)) return 0;
        *out = -a;
        return 1;
    case VHDL_R_name:
        if (n->child->next && n->child->next->rule == VHDL_R_attr && !n->child->next->next)
            return vi_attr(n->child, n->child->next->child, out);
        return 0;
    case VHDL_R_simple:
    case VHDL_R_term:
        k = n->child;
        if (!vi_ceval(k, &a)) return 0;
        for (k = k->next; k; k = k->next->next) {
            if (!vi_ceval(k->next, &b)) return 0;
            if (vi_is(k, "+")) a += b;
            else if (vi_is(k, "-")) a -= b;
            else if (vi_is(k, "*")) a *= b;
            else if (vi_is(k, "/") || vi_is(k, "mod") || vi_is(k, "rem")) {
                if (!b) vi_fail(k, "division by zero");
                if (vi_is(k, "/")) a /= b;
                else if (vi_is(k, "rem")) a %= b;
                else { a %= b; if (a && ((a < 0) != (b < 0))) a += b; }
            } else if (vi_is(k, "**")) {
                long r = 1;
                if (b < 0 || b > 31) return 0;
                while (b--) r *= a;
                a = r;
            } else return 0;
        }
        *out = a;
        return 1;
    default:
        return 0;
    }
}

static long vi_const_int(const gram_node *n, const char *what)
{
    long v;
    if (!vi_ceval(n, &v)) vi_fail(n, "%s must be a constant: a number, a generic or a constant", what);
    return v;
}

/* ---- types --------------------------------------------------------------- */

/* type -> ty, width and range */
static VType *vi_find_type(const gram_node *id, int *index)
{
    int i;
    for (i = vi_ntypes - 1; i >= 0; i--)
        if (vi_is(id, vi_fmt("%.*s", (int)vi_types[i].id->len, vi_types[i].id->text))) { if (index) *index = i; return &vi_types[i]; }
    return NULL;
}

/* allow_int: 0 no integers, 1 integer constants, 2 integer signals and variables (with a range) */
static void vi_type(gram_node *t, VSym *s, int allow_int)
{
    gram_node *id = t->child, *r = id->next;
    int ti;
    VType *ut = vi_find_type(id, &ti);
    if (ut) {
        if (r) vi_fail(r, "%.*s has no range of its own", VT(id));
        s->ut = ti + 1;
        if (ut->isarr) { s->ty = T_ARR; s->w = ut->elem.w; }
        else { s->ty = T_ENUM; s->w = ut->w; s->left = ut->w - 1; s->right = 0; s->down = 1; }
        return;
    }
    if ((vi_is(id, "std_logic") || vi_is(id, "std_ulogic") || vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector"))
        && !(vi_ctx & CTX_1164))
        vi_fail(id, "%.*s is not visible here: put library ieee; use ieee.std_logic_1164.all; before this entity "
                    "(each entity needs its own)", VT(id));
    if ((vi_is(id, "unsigned") || vi_is(id, "signed")) && !(vi_ctx & CTX_NUM))
        vi_fail(id, "%.*s is not visible here: put use ieee.numeric_std.all; before this entity (each entity needs its own)",
                VT(id));
    if (vi_is(id, "std_logic") || vi_is(id, "std_ulogic") || vi_is(id, "bit")) {
        if (r) vi_fail(r, "%.*s is one bit and has no range", VT(id));
        s->ty = T_SL; s->w = 1;
        return;
    }
    if (vi_is(id, "integer") || vi_is(id, "natural") || vi_is(id, "positive")) {
        if (!allow_int)
            vi_fail(id, "Wire needs a width here: use std_logic_vector(n-1 downto 0) or unsigned(n-1 downto 0) "
                        "instead of %.*s", VT(id));
        if (allow_int == 2) {   /* an integer signal or variable: unsigned bits, as many as its range needs */
            long lo, hi;
            if (!r) vi_fail(id, "give this %.*s a range, like %.*s range 0 to 9, so Wire knows how many bits it has", VT(id), VT(id));
            lo = vi_const_int(r->child, "a range");
            hi = vi_const_int(r->child->next->next, "a range");
            if (vi_is(r->child->next, "downto")) { long tmp = lo; lo = hi; hi = tmp; }
            if (lo < 0) vi_fail(r, "integers below 0 are not supported yet; use signed");
            if (hi < lo) vi_fail(r, "this range is empty");
            s->ty = T_INTV; s->w = 1;
            while (s->w < 31 && (1L << s->w) <= hi) s->w++;
            s->left = s->w - 1; s->right = 0; s->down = 1;
            return;
        }
        s->ty = T_INT; s->w = 0;
        return;
    }
    if (vi_is(id, "boolean")) {
        if (r) vi_fail(r, "boolean is one value and has no range");
        s->ty = T_BOOL; s->w = 1;
        return;
    }
    if (vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector") || vi_is(id, "bit_vector")) s->ty = T_SLV;
    else if (vi_is(id, "unsigned")) s->ty = T_UNS;
    else if (vi_is(id, "signed")) s->ty = T_SGN;
    else vi_fail(id, "Wire does not know the type %.*s (it reads std_logic, std_logic_vector, unsigned, "
                     "signed, and integer constants)", VT(id));
    if (!r || r->rule != VHDL_R_drange) vi_fail(id, "give %.*s a range, like %.*s(7 downto 0)", VT(id), VT(id));
    s->left = (int)vi_const_int(r->child, "a range");
    s->right = (int)vi_const_int(r->child->next->next, "a range");
    s->down = vi_is(r->child->next, "downto");
    s->w = s->down ? s->left - s->right + 1 : s->right - s->left + 1;
    if (s->w < 1) vi_fail(r, "this range is empty");
    if (s->w > MAXW) vi_fail(r, "Wire handles up to %d bits, and this is %d; wider values are not supported yet", MAXW, s->w);
}

static int vi_num(int ty)        /* can do arithmetic */
{
    return ty == T_UNS || ty == T_SGN || ty == T_INTV || (ty == T_SLV && vi_slv_num);
}

static int vi_signed(int ty) { return ty == T_SGN || (ty == T_SLV && vi_slv_num == 2); }

/* ---- literals ------------------------------------------------------------ */

static unsigned long vi_mask(int w) { return w >= 32 ? 0xffffffffUL : (1UL << w) - 1; }

static char *vi_lit(unsigned long v, int w)
{
    char *s = (char *)malloc((size_t)w + 4);
    int i, p = 0;
    v &= vi_mask(w);
    if (w >= 8 && w % 4 == 0) {
        s[p++] = '0'; s[p++] = 'x';
        for (i = w / 4 - 1; i >= 0; i--) s[p++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
    } else {
        s[p++] = '0'; s[p++] = 'b';
        for (i = w - 1; i >= 0; i--) s[p++] = (char)('0' + ((v >> i) & 1));
    }
    s[p] = 0;
    return s;
}

static VX vi_const(unsigned long v, int w, int ty)
{
    VX x;
    memset(&x, 0, sizeof x);
    x.ty = ty; x.w = w; x.cst = 1; x.v = v & vi_mask(w);
    x.s = vi_lit(x.v, w);
    x.atom = 2;
    return x;
}

static int vi_bitchar(const gram_node *at, int c)
{
    c = vi_lower(c);
    if (c == '1' || c == 'h') return 1;
    if (c == '0' || c == 'l' || c == '-' || c == 'x' || c == 'u' || c == 'w') return 0;
    if (c == 'z') vi_fail(at, "'Z' (tri-state) is not supported yet");
    vi_fail(at, "'%c' is not a value of std_logic", c);
    return 0;
}

static VX vi_strlit(const gram_node *n)
{
    unsigned long v = 0;
    size_t i;
    int w = (int)n->len - 2;
    if (w < 1) vi_fail(n, "an empty string is not a value");
    if (w > MAXW) vi_fail(n, "Wire handles up to %d bits, and this is %d; wider values are not supported yet", MAXW, w);
    for (i = 1; i + 1 < n->len; i++) v = (v << 1) | (unsigned long)vi_bitchar(n, n->text[i]);
    return vi_const(v, w, T_STR);
}

static VX vi_bitstr(const gram_node *n)
{
    size_t i = 0;
    long width = -1;
    int base, bits = 0, c;
    unsigned long v = 0;
    while (n->text[i] >= '0' && n->text[i] <= '9') { width = (width < 0 ? 0 : width * 10) + (n->text[i] - '0'); i++; }
    c = vi_lower(n->text[i]);
    base = c == 'b' ? 1 : c == 'o' ? 3 : c == 'x' ? 4 : 0;
    for (i += 2; i + 1 < n->len; i++) {
        int d;
        c = vi_lower(n->text[i]);
        if (c == '_') continue;
        d = c >= 'a' ? c - 'a' + 10 : c - '0';
        if (base ? d >= (1 << base) : d > 9) vi_fail(n, "%.*s has a digit that does not belong", VT(n));
        if (base) { v = (v << base) | (unsigned long)d; bits += base; }
        else v = v * 10 + (unsigned long)d;
    }
    if (!base) {
        if (width < 0) vi_fail(n, "a d\"...\" literal needs its width in front, like 8d\"200\"");
        bits = 0;
    }
    if (width >= 0) {
        if (width < 1 || width > MAXW) vi_fail(n, "Wire handles 1 to %d bits", MAXW);
        if (base && bits > width && (v >> width)) vi_fail(n, "%.*s does not fit in %ld bits", VT(n), width);
        if (!base && width < 32 && (v >> width)) vi_fail(n, "%.*s does not fit in %ld bits", VT(n), width);
        bits = (int)width;
    }
    if (bits < 1) vi_fail(n, "an empty literal is not a value");
    if (bits > MAXW) vi_fail(n, "Wire handles up to %d bits, and this is %d; wider values are not supported yet", MAXW, bits);
    return vi_const(v, bits, T_STR);
}

/* ---- expressions ----------------------------------------------------------- */

static VX vi_gen(gram_node *n, int want);
static VX vi_var_read(VSym *s, gram_node *at);
static VX vi_array_read(VSym *s, gram_node *id, gram_node *args, int want);
static VX vi_barrel(VX x, VX amount, int left, int arith, int rot, gram_node *at);

static char *vi_wrap(const VX *x, int need)      /* x as an operand that needs atom level need */
{
    return x->atom >= need ? x->s : vi_fmt("(%s)", x->s);
}

static VX vi_mk(char *s, int ty, int w, int atom)
{
    VX x;
    memset(&x, 0, sizeof x);
    x.s = s; x.ty = ty; x.w = w; x.atom = atom;
    return x;
}

static const char *vi_desc(const VX *x)
{
    if (x->ty == T_SL || x->ty == T_BOOL) return x->ty == T_SL ? "1 bit (std_logic)" : "a condition (boolean)";
    if (x->ty == T_INT) return "an integer";
    return vi_fmt("%d bit%s", x->w, x->w == 1 ? "" : "s");
}

/* x widened (or cut) to w bits, as unsigned or signed */
static VX vi_resize(VX x, int w, int sgn)
{
    VBuf b = { 0, 0, 0 };
    int i;
    if (x.w == w) return x;
    if (x.cst) {
        unsigned long v = x.v;
        if (sgn && w > x.w && ((v >> (x.w - 1)) & 1)) v |= vi_mask(w) & ~vi_mask(x.w);
        return vi_const(v, w, x.ty);
    }
    if (w < x.w) return vi_mk(vi_fmt("%s[%d:0]", vi_wrap(&x, 2), w - 1), x.ty, w, 2);
    vb_add(&b, "{", 1);
    if (sgn) {
        char *top = x.w == 1 ? vi_wrap(&x, 2) : vi_fmt("%s[%d]", vi_wrap(&x, 2), x.w - 1);
        for (i = x.w; i < w; i++) vbf(&b, "%s, ", top);
    } else vbf(&b, "%s, ", vi_lit(0, w - x.w));
    vbf(&b, "%s}", x.s);
    {
        VX r = vi_mk(b.s, x.ty, w, 2);
        r.cat = 1;
        if (!sgn) { r.zx = x.s; r.zxw = x.w; }
        return r;
    }
}

/* an integer used where a vector of w bits is */
static VX vi_int_as(const VX *i, int w, int ty, gram_node *at)
{
    long v = i->iv;
    if (vi_signed(ty) ? (w < 32 && (v < -(1L << (w - 1)) || v >= (1L << (w - 1))))
                      : (v < 0 || (w < 32 && v >= (1L << w))))
        vi_fail(at, "%ld does not fit in %d bits (%s)", v, w, vi_tyname[ty]);
    return vi_const((unsigned long)v, w, ty);
}

static void vi_need_value(const VX *x, gram_node *at)
{
    if (x->ty == T_INT) vi_fail(at, "an integer can only be used next to an unsigned or signed value here");
}

/* index or slice of a signal: x(3), x(7 downto 4) */
static VX vi_index(VSym *s, gram_node *id, gram_node *arg)
{
    int lo = vi_low(s), hi = vi_high(s);
    char *base = s->kind == VK_CONST ? vi_lit(s->bits, s->w) : s->wname;
    if (!vi_vec(s->ty)) vi_fail(id, "%.*s is one bit; it has no bits to pick", VT(id));
    if (arg->rule == VHDL_R_drange) {
        long a = vi_const_int(arg->child, "a slice"), b = vi_const_int(arg->child->next->next, "a slice");
        int down = vi_is(arg->child->next, "downto"), wa, wb;
        if (down != s->down)
            vi_fail(arg, "%.*s is declared with %s, so slice it with %s too", VT(id),
                    s->down ? "downto" : "to", s->down ? "downto" : "to");
        if ((down && a < b) || (!down && a > b)) vi_fail(arg, "this slice is empty");
        if (a < lo || a > hi || b < lo || b > hi)
            vi_fail(arg, "this slice is outside %.*s(%d %s %d)", VT(id), s->left, s->down ? "downto" : "to", s->right);
        wa = s->down ? (int)a - lo : hi - (int)a;
        wb = s->down ? (int)b - lo : hi - (int)b;
        if (s->kind == VK_CONST) {
            int top = wa > wb ? wa : wb, bot = wa > wb ? wb : wa;
            return vi_const(s->bits >> bot, top - bot + 1, s->ty);
        }
        if (wa == s->w - 1 && wb == 0) return vi_mk(base, s->ty, s->w, 2);
        return vi_mk(vi_fmt("%s[%d:%d]", base, wa > wb ? wa : wb, wa > wb ? wb : wa), s->ty, (wa > wb ? wa - wb : wb - wa) + 1, 2);
    } else {
        long i;
        int k;
        if (!vi_ceval(arg, &i)) {   /* a bit picked by a signal: a mux over the bits */
            VX ix = vi_gen(arg, -1);
            VBuf b = { 0, 0, 0 };
            int j, n;
            if (!strcmp(base, "@")) vi_fail(arg, "a bit picked by a signal cannot be used here");
            if (ix.ty != T_INTV && ix.ty != T_UNS) vi_fail(arg, "an index is an integer: write %.*s(to_integer(...))", VT(id));
            if (ix.w > 5) vi_fail(arg, "picking one of more than 32 bits with a signal is not supported yet");
            n = 1 << ix.w;
            vb_add(&b, "mux(", 4);
            for (j = 0; j < n; j++) {
                if (j < lo || j > hi) { vb_add(&b, "0b0, ", 5); continue; }
                k = s->down ? j - lo : hi - j;
                if (s->kind == VK_CONST) vbf(&b, "0b%lu, ", (s->bits >> k) & 1);
                else vbf(&b, "%s[%d], ", base, k);
            }
            vbf(&b, "sel = %s)", ix.s);
            return vi_mk(b.s, T_SL, 1, 2);
        }
        if (i < lo || i > hi)
            vi_fail(arg, "%ld is outside %.*s(%d %s %d)", i, VT(id), s->left, s->down ? "downto" : "to", s->right);
        k = s->down ? (int)i - lo : hi - (int)i;
        if (s->kind == VK_CONST) return vi_const((s->bits >> k) & 1, 1, T_SL);
        return vi_mk(vi_fmt("%s[%d]", base, k), T_SL, 1, 2);
    }
}

static gram_node *vi_arg(gram_node *args, int i)
{
    gram_node *a = args->child;
    while (a && i--) a = a->next;
    return a;
}

static int vi_nargs(gram_node *args)
{
    int n = 0;
    gram_node *a;
    for (a = args->child; a; a = a->next) n++;
    return n;
}

static VX vi_shift(VX x, int left, int arith, int rot, long k, gram_node *at);

static char *vi_sl(const char *b, int hi, int lo)   /* b[hi:lo], or b[hi] for one bit */
{
    return hi == lo ? vi_fmt("%s[%d]", b, hi) : vi_fmt("%s[%d:%d]", b, hi, lo);
}

/* f(...): a conversion, or a bit or slice of a signal */
static gram_node *vi_find_func(const gram_node *id, int nargs);
static int vi_bitrange(VSym *s, gram_node *id, gram_node *arg, int *hi, int *lo);
static VX vi_func_call(gram_node *f, gram_node *id, gram_node *args);

static VX vi_call(gram_node *n, int want)
{
    gram_node *id = n->child, *args = id->next;
    VSym *s = vi_find(id);
    VX x;
    long k;
    memset(&x, 0, sizeof x);
    if (args->rule == VHDL_R_attr) {
        if (vi_ceval(n, &k)) { x = vi_mk(vi_fmt("%ld", k), T_INT, 0, 2); x.cst = 1; x.iv = k; return x; }
        if (vi_is(args->child, "event") || vi_is(args->child, "stable"))
            vi_fail(args, "'%.*s is about clocks, which belong in a process (not supported yet)", VT(args->child));
        vi_fail(args, "Wire does not know the attribute '%.*s here", VT(args->child));
    }
    if (args->rule == VHDL_R_qual) {   /* unsigned'("1001"): the value, as that type */
        int to = vi_is(id, "unsigned") ? T_UNS : vi_is(id, "signed") ? T_SGN
               : vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector") ? T_SLV
               : vi_is(id, "std_logic") || vi_is(id, "std_ulogic") ? T_SL : -1;
        if (args->next) vi_fail(args->next, "one thing at a time, please");
        if (to < 0) vi_fail(id, "Wire does not know the type %.*s", VT(id));
        x = vi_gen(args->child, want);
        if ((to == T_SL) != (x.ty == T_SL || (x.ty == T_STR && x.w == 1 && args->child->rule == VHDL_R_chr)))
            vi_fail(args->child, "this is not a %.*s", VT(id));
        if (x.ty == T_INT) vi_fail(args->child, "an integer is not a %.*s", VT(id));
        x.ty = to;
        return x;
    }
    if (s && s->ty == T_ARR) return vi_array_read(s, id, args, want);
    if (!s && args->rule == VHDL_R_args) {
        gram_node *f = vi_find_func(id, vi_nargs(args));
        if (f) {
            if (args->next) vi_fail(args->next, "one thing at a time, please: give the function's result a name first");
            return vi_func_call(f, id, args);
        }
    }
    if (args->next) vi_fail(args->next, "one index or slice at a time, please");
    if (s && s->kind == VK_LABEL) vi_fail(id, "%.*s is the label of an instance, not a signal", VT(id));
    if (s && s->kind == VK_VAR) {   /* a bit or slice of a variable: of its value so far */
        VX v = vi_var_read(s, id);
        VSym tmp = *s;
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        tmp.kind = v.cst ? VK_CONST : VK_SIG;
        tmp.bits = v.v;
        if (v.csum && vi_vec(s->ty)) {   /* the sum or the carry, without a split */
            int hi, lo, one = vi_bitrange(s, id, args->child, &hi, &lo);
            if (!one && hi == v.w - 2 && lo == 0) return vi_mk(v.csum, s->ty, v.w - 1, 2);
            if (one && hi == v.w - 1) return vi_mk(vi_fmt("%s.cout", v.csum), T_SL, 1, 2);
        }
        tmp.wname = vi_wrap(&v, 2);
        return vi_index(&tmp, id, args->child);
    }
    if (s && s->kind != VK_GEN && !(s->kind == VK_CONST && s->ty == T_INT)) {
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        return vi_index(s, id, args->child);
    }
    if (s) vi_fail(id, "%.*s is an integer; it has no bits to pick", VT(id));
    {   /* numeric_std's functions need its use clause, as in Vivado */
        static const char *num[] = { "unsigned", "signed", "to_unsigned", "to_signed", "resize", "shift_left", "shift_right",
                                     "rotate_left", "rotate_right", "conv_std_logic_vector", "conv_unsigned", "conv_signed", NULL };
        int j;
        for (j = 0; num[j]; j++)
            if (vi_is(id, num[j]) && !(vi_ctx & CTX_NUM))
                vi_fail(id, "%.*s is not visible here: put use ieee.numeric_std.all; before this entity (each entity "
                            "needs its own)", VT(id));
    }
    if (vi_is(id, "unsigned") || vi_is(id, "signed") || vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector")) {
        int to = vi_is(id, "unsigned") ? T_UNS : vi_is(id, "signed") ? T_SGN : T_SLV;
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s() takes one value", VT(id));
        x = vi_gen(args->child, want);
        if (!vi_vec(x.ty))
            vi_fail(args->child, "%.*s() converts a vector, but this is %s%s", VT(id), vi_desc(&x),
                    x.ty == T_INT ? (to == T_SLV ? "; use std_logic_vector(to_unsigned(x, n))"
                                                 : to == T_UNS ? "; use to_unsigned(x, n)" : "; use to_signed(x, n)") : "");
        x.ty = to;
        return x;
    }
    if (vi_is(id, "to_unsigned") || vi_is(id, "to_signed") || vi_is(id, "conv_std_logic_vector")
        || vi_is(id, "conv_unsigned") || vi_is(id, "conv_signed")) {
        int to = vi_is(id, "to_signed") || vi_is(id, "conv_signed") ? T_SGN : vi_is(id, "conv_std_logic_vector") ? T_SLV : T_UNS;
        long w;
        VX v;
        if (vi_nargs(args) != 2) vi_fail(args, "%.*s takes a value and a width: %.*s(5, 4)", VT(id), VT(id));
        w = vi_const_int(vi_arg(args, 1), "the width");
        if (w < 1 || w > MAXW) vi_fail(vi_arg(args, 1), "Wire handles 1 to %d bits", MAXW);
        v = vi_gen(vi_arg(args, 0), -1);
        if (v.ty == T_INTV) {   /* an integer signal or variable: its bits, to that width */
            x = vi_resize(v, (int)w, 0);
            x.ty = to;
            return x;
        }
        if (v.ty != T_INT || !v.cst)
            vi_fail(vi_arg(args, 0), "%.*s takes an integer", VT(id));
        if (to == T_UNS && v.iv < 0) vi_fail(vi_arg(args, 0), "%ld is negative; unsigned cannot hold it", v.iv);
        x = vi_int_as(&v, (int)w, to == T_SLV ? (v.iv < 0 ? T_SGN : T_UNS) : to, vi_arg(args, 0));
        x.ty = to;
        return x;
    }
    if (vi_is(id, "resize")) {
        long w;
        if (vi_nargs(args) != 2) vi_fail(args, "resize takes a value and a width: resize(x, 8)");
        w = vi_const_int(vi_arg(args, 1), "the width");
        if (w < 1 || w > MAXW) vi_fail(vi_arg(args, 1), "Wire handles 1 to %d bits", MAXW);
        x = vi_gen(vi_arg(args, 0), -1);
        if (x.ty != T_UNS && x.ty != T_SGN) vi_fail(vi_arg(args, 0), "resize works on unsigned or signed");
        if (x.ty == T_SGN && w < x.w) {   /* numeric_std keeps the sign bit */
            VX lo = vi_resize(x, (int)w - 1, 0), top;
            if (w == 1) return vi_mk(vi_fmt("%s[%d]", vi_wrap(&x, 2), x.w - 1), T_SGN, 1, 2);
            top = vi_mk(vi_fmt("%s[%d]", vi_wrap(&x, 2), x.w - 1), T_SGN, 1, 2);
            x = vi_mk(vi_fmt("{%s, %s}", top.s, lo.s), T_SGN, (int)w, 2);
            x.cat = 1;
            return x;
        }
        return vi_resize(x, (int)w, x.ty == T_SGN);
    }
    if (vi_is(id, "shift_left") || vi_is(id, "shift_right") || vi_is(id, "rotate_left") || vi_is(id, "rotate_right")) {
        if (vi_nargs(args) != 2) vi_fail(args, "%.*s takes a value and a count", VT(id));
        x = vi_gen(vi_arg(args, 0), want);
        if (x.ty != T_UNS && x.ty != T_SGN) vi_fail(vi_arg(args, 0), "%.*s works on unsigned or signed", VT(id));
        if (!vi_ceval(vi_arg(args, 1), &k))   /* by a signal: a barrel shifter */
            return vi_barrel(x, vi_gen(vi_arg(args, 1), -1), vi_is(id, "shift_left") || vi_is(id, "rotate_left"), x.ty == T_SGN,
                             vi_is(id, "rotate_left") || vi_is(id, "rotate_right"), n);
        return vi_shift(x, vi_is(id, "shift_left") || vi_is(id, "rotate_left"), x.ty == T_SGN,
                        vi_is(id, "rotate_left") || vi_is(id, "rotate_right"), k, n);
    }
    if (vi_is(id, "to_integer") || vi_is(id, "conv_integer")) {   /* the same bits, as an integer */
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one value", VT(id));
        x = vi_gen(args->child, -1);
        if (x.ty == T_INT || x.ty == T_INTV) return x;
        if (x.ty == T_SGN) vi_fail(args->child, "to_integer of a signed value is not supported yet");
        if (x.ty != T_UNS && !(x.ty == T_SLV && (vi_slv_num || vi_is(id, "conv_integer"))))
            vi_fail(args->child, "%.*s takes an unsigned value: write %.*s(unsigned(...))", VT(id), VT(id));
        if (x.cst) { VX c; memset(&c, 0, sizeof c); c.ty = T_INT; c.cst = 1; c.iv = (long)x.v; c.s = vi_fmt("%ld", c.iv); c.atom = 2; return c; }
        x.ty = T_INTV;
        return x;
    }
    if (vi_is(id, "rising_edge") || vi_is(id, "falling_edge"))
        vi_fail(id, "%.*s goes in a clocked process: if rising_edge(clk) then ... end if;", VT(id));
    vi_fail(id, "there is no signal or function called %.*s", VT(id));
    return x;
}

static VX vi_shift(VX x, int left, int arith, int rot, long k, gram_node *at)
{
    int w = x.w;
    char *b = vi_wrap(&x, 2);
    if (k < 0) { left = !left; k = -k; }
    if (rot) k %= w;
    if (k == 0) return x;
    if (x.cst) {
        unsigned long v = x.v, m = vi_mask(w);
        if (rot) v = left ? ((v << k) | (v >> (w - k))) & m : ((v >> k) | (v << (w - k))) & m;
        else if (k >= w) v = arith && !left && ((v >> (w - 1)) & 1) ? m : 0;
        else if (left) v = (v << k) & m;
        else { int top = (v >> (w - 1)) & 1; v >>= k; if (arith && top) v |= m & ~(m >> k); }
        return vi_const(v, w, x.ty);
    }
    (void)at;
    if (rot) {
        x = left ? vi_mk(vi_fmt("{%s, %s}", vi_sl(b, w - 1 - (int)k, 0), vi_sl(b, w - 1, w - (int)k)), x.ty, w, 2)
                 : vi_mk(vi_fmt("{%s, %s}", vi_sl(b, (int)k - 1, 0), vi_sl(b, w - 1, (int)k)), x.ty, w, 2);
    } else if (k >= w) {
        if (arith && !left) {
            VBuf s = { 0, 0, 0 };
            int i;
            vb_add(&s, "{", 1);
            for (i = 0; i < w; i++) vbf(&s, "%s%s[%d]", i ? ", " : "", b, w - 1);
            vb_add(&s, "}", 1);
            x = vi_mk(s.s, x.ty, w, 2);
        } else return vi_const(0, w, x.ty);
    } else if (left) {
        x = vi_mk(vi_fmt("{%s, %s}", vi_sl(b, w - 1 - (int)k, 0), vi_lit(0, (int)k)), x.ty, w, 2);
    } else if (arith) {
        VBuf s = { 0, 0, 0 };
        int i;
        vb_add(&s, "{", 1);
        for (i = 0; i < k; i++) vbf(&s, "%s[%d], ", b, w - 1);
        vbf(&s, "%s}", vi_sl(b, w - 1, (int)k));
        x = vi_mk(s.s, x.ty, w, 2);
    } else {
        x = vi_mk(vi_fmt("{%s, %s}", vi_lit(0, (int)k), vi_sl(b, w - 1, (int)k)), x.ty, w, 2);
    }
    x.cat = 1;
    return x;
}

/* (others => '0'), (7 => '1', others => '0'), ('1', '0', a, b) */
static VX vi_aggregate(gram_node *n, int want)
{
    char **bit;
    int i, pos = 0, named = 0, w = want;
    gram_node *el, *others = NULL;
    VBuf b = { 0, 0, 0 };
    long top = -1;
    for (el = n->child; el && top != -2; el = el->next) {   /* only named bits and no others: its highest bit */
        gram_node *c;
        long k;
        if (el->child->rule != VHDL_R_choices) { top = -2; break; }
        for (c = el->child->child; c; c = c->next) {
            long k2;
            if (c->rule == VHDL_R_drange && vi_ceval(c->child, &k) && vi_ceval(c->child->next->next, &k2)) {
                if (k2 > k) k = k2;
            } else if (c->rule == VHDL_R_OTHERS || !vi_ceval(c, &k)) { top = -2; break; }
            if (k > top) top = k;
        }
    }
    if (top >= 0 && (w < 1 || top + 1 < w)) w = (int)top + 1;   /* like unsigned'(0 => '1') */
    else if (w < 1) {
        for (el = n->child; el; el = el->next) {
            if (el->child->rule == VHDL_R_choices) break;
            pos++;
        }
        if (el || pos < 2) vi_fail(n, "Wire cannot tell how wide this aggregate is here; assign it to a signal first");
        w = pos;
        pos = 0;
    }
    bit = (char **)calloc((size_t)w, sizeof *bit);
    for (el = n->child; el; el = el->next) {
        gram_node *ch = el->child->rule == VHDL_R_choices ? el->child : NULL;
        gram_node *e = ch ? ch->next : el->child, *c;
        VX v = vi_gen(e, 1);
        if (v.ty == T_INT || v.w != 1) vi_fail(e, "each element here is one bit, like '0', '1' or a std_logic signal");
        if (!ch) {
            if (named) vi_fail(e, "put the elements without a name before the named ones");
            if (pos >= w) vi_fail(e, "this aggregate has more than %d elements", w);
            bit[w - 1 - pos++] = v.s;
            continue;
        }
        named = 1;
        for (c = ch->child; c; c = c->next) {
            long k;
            if (c->rule == VHDL_R_OTHERS) {   /* VHDL puts others last */
                if (others) vi_fail(c, "others is given twice");
                others = c;
                for (i = 0; i < w; i++) if (!bit[i]) bit[i] = v.s;
                continue;
            }
            if (others) vi_fail(c, "others must be the last choice");
            if (c->rule == VHDL_R_drange) {   /* 7 downto 0 => e */
                long a = vi_const_int(c->child, "a range"), z = vi_const_int(c->child->next->next, "a range"), j;
                if (a > z) { long t = a; a = z; z = t; }
                if (a < 0 || z >= w) vi_fail(c, "this range is outside 0 to %d", w - 1);
                for (j = a; j <= z; j++) {
                    if (bit[j]) vi_fail(c, "bit %ld is given twice", j);
                    bit[j] = v.s;
                }
                continue;
            }
            if (!vi_ceval(c, &k)) vi_fail(c, "a choice in an aggregate must be a constant bit number");
            if (k < 0 || k >= w) vi_fail(c, "bit %ld is outside 0 to %d", k, w - 1);
            if (bit[k]) vi_fail(c, "bit %ld is given twice", k);
            bit[k] = v.s;
        }
    }
    for (i = 0; i < w; i++) if (!bit[i]) vi_fail(n, "bit %d of this aggregate has no value; add others => '0'", i);
    {   /* all constants: one literal */
        unsigned long v = 0;
        for (i = w - 1; i >= 0; i--) {
            if (strcmp(bit[i], "0b0") && strcmp(bit[i], "0b1")) break;
            v = (v << 1) | (unsigned long)(bit[i][2] == '1');
        }
        if (i < 0) return vi_const(v, w, T_STR);
    }
    vb_add(&b, "{", 1);
    for (i = w - 1; i >= 0; ) {   /* runs of constant bits become one literal */
        int j = i;
        unsigned long v = 0;
        while (j >= 0 && (!strcmp(bit[j], "0b0") || !strcmp(bit[j], "0b1"))) { v = (v << 1) | (unsigned long)(bit[j][2] == '1'); j--; }
        vbf(&b, "%s%s", i == w - 1 ? "" : ", ", j < i ? vi_lit(v, i - j) : bit[i]);
        i = j < i ? j : i - 1;
    }
    vb_add(&b, "}", 1);
    {
        VX x = vi_mk(b.s, T_STR, w, 2);
        x.cat = 1;
        return x;
    }
}

/* a and b, a or b ...: all the same operator, as VHDL wants */
static VX vi_logic(gram_node *n, int want)
{
    gram_node *k, *op = n->child->next;
    VX x[64];
    int m = 0, i, w = -1, ty = -1, bool_ = 0;
    VBuf b = { 0, 0, 0 };
    for (k = op; k; k = k->next->next) {
        size_t j;
        int same = k->len == op->len;
        for (j = 0; same && j < k->len; j++) if (vi_lower(k->text[j]) != vi_lower(op->text[j])) same = 0;
        if (!same) vi_fail(k, "VHDL needs parentheses when %.*s and %.*s are mixed", VT(op), VT(k));
    }
    if (n->nchildren > 3 && (vi_is(op, "nand") || vi_is(op, "nor") || vi_is(op, "xnor")))
        vi_fail(op, "VHDL needs parentheses around each %.*s when there are more than two", VT(op));
    for (k = n->child; k; k = k->next ? k->next->next : NULL) {
        if (m == 64) vi_fail(k, "too many operands");
        x[m] = vi_gen(k, want);
        vi_need_value(&x[m], k);
        if (x[m].ty == T_BOOL) bool_ = 1;
        if (x[m].ty != T_STR && ty < 0) ty = x[m].ty;
        m++;
    }
    for (i = 0; i < m; i++) if (x[i].ty != T_STR && x[i].ty != T_BOOL) { w = x[i].w; break; }
    if (w < 0) w = x[0].w;
    for (i = 0, k = n->child; i < m; i++, k = k->next ? k->next->next : NULL) {
        int sc = x[i].ty == T_SL || x[i].ty == T_BOOL;
        if (x[i].w != w)
            vi_fail(k, "both sides of %.*s must be the same width: this is %s, the other side %d bit%s",
                    VT(op), vi_desc(&x[i]), w, w == 1 ? "" : "s");
        if (sc != (x[0].ty == T_SL || x[0].ty == T_BOOL))
            vi_fail(k, "%.*s cannot mix a single bit with a vector", VT(op));
    }
    if (bool_) ty = T_BOOL;
    if (ty < 0) ty = T_STR;
    if (vi_is(op, "nand") || vi_is(op, "nor") || vi_is(op, "xnor")) {
        char kw[8];
        snprintf(kw, sizeof kw, "%c%c%c%c", vi_lower(op->text[0]), vi_lower(op->text[1]), vi_lower(op->text[2]),
                 op->len > 3 ? vi_lower(op->text[3]) : 0);
        return vi_mk(vi_fmt("%s(%s, %s)", kw, x[0].s, x[1].s), ty, w, 2);
    }
    for (i = 0; i < m; i++)
        vbf(&b, "%s%s", i ? (vi_is(op, "and") ? " & " : vi_is(op, "or") ? " | " : " ^ ") : "", vi_wrap(&x[i], 1));
    return vi_mk(b.s, ty, w, 0);
}

/* the two sides of a comparison or of + and -, made the same width */
static void vi_pair(VX *a, VX *b, gram_node *an, gram_node *bn, const char *op, int arith)
{
    int ty, w;
    if (a->ty == T_ENUM || b->ty == T_ENUM) {
        if (a->ty != b->ty || a->tid != b->tid)
            vi_fail(bn, "%s compares a state with something else (%s)", op, vi_tyname[a->ty == T_ENUM ? b->ty : a->ty]);
        if (arith) vi_fail(bn, "states cannot be added or subtracted");
        return;
    }
    if (a->ty == T_INT && b->ty == T_INT) return;
    if ((a->ty == T_INTV) != (b->ty == T_INTV) && a->ty != T_INT && b->ty != T_INT) {
        /* unsigned and integer: the integer takes the unsigned's width, as numeric_std does */
        VX *iv = a->ty == T_INTV ? a : b, *o = iv == a ? b : a;
        if (vi_num(o->ty) || o->ty == T_STR) { *iv = vi_resize(*iv, o->w, 0); iv->ty = o->ty == T_STR ? T_UNS : o->ty; }
    }
    if (a->ty == T_INT || b->ty == T_INT) {
        VX *i = a->ty == T_INT ? a : b, *v = a->ty == T_INT ? b : a;
        gram_node *in = a->ty == T_INT ? an : bn, *vn = a->ty == T_INT ? bn : an;
        if (!vi_num(v->ty)) {
            if (v->ty == T_SLV)
                vi_fail(vn, "%s with an integer needs unsigned or signed: write unsigned(...), or use "
                            "ieee.std_logic_unsigned", op);
            vi_fail(vn, "%s with an integer needs unsigned or signed, but this is %s", op, vi_desc(v));
        }
        *i = vi_int_as(i, v->w, v->ty, in);
        return;
    }
    if (a->ty == T_BOOL || b->ty == T_BOOL || a->ty == T_SL || b->ty == T_SL) {
        if (arith) {   /* numeric_std (2008) adds a std_logic as a 1-bit number */
            VX *s = (a->ty == T_SL || a->ty == T_BOOL) ? a : b, *v = s == a ? b : a;
            if (s->ty == T_BOOL) vi_fail(s == a ? an : bn, "a condition is not a number");
            if (v->ty == T_SL) vi_fail(an, "%s needs unsigned or signed values, not single bits", op);
            s->ty = v->ty;
        } else {
            if (a->w != b->w) vi_fail(bn, "%s compares %s with %s", op, vi_desc(a), vi_desc(b));
            return;
        }
    }
    ty = a->ty != T_STR ? a->ty : b->ty;
    if (a->ty != T_STR && b->ty != T_STR && a->ty != b->ty && !(arith && (a->ty == T_SLV || b->ty == T_SLV) && vi_slv_num))
        vi_fail(bn, "%s needs both sides of the same type: this is %s, the other side %s (convert one with "
                    "unsigned(), signed() or std_logic_vector())", op, vi_tyname[b->ty], vi_tyname[a->ty]);
    if (arith) {
        if (!vi_num(ty) && ty != T_STR) {
            vi_fail(an, "%s needs unsigned or signed values (use ieee.numeric_std and write unsigned(...)), "
                        "but this is %s", op, vi_tyname[ty]);
        }
        if (ty == T_STR) ty = T_UNS;
        w = a->w > b->w ? a->w : b->w;
        if (a->ty == T_STR && b->ty == T_STR) w = a->w > b->w ? a->w : b->w;
        *a = vi_resize(*a, w, vi_signed(ty));
        *b = vi_resize(*b, w, vi_signed(ty));
        a->ty = b->ty = ty;
        return;
    }
    if (a->w != b->w) {
        if (vi_num(ty) && a->ty != T_STR && b->ty != T_STR) {   /* numeric_std compares the values */
            w = a->w > b->w ? a->w : b->w;
            *a = vi_resize(*a, w, vi_signed(ty));
            *b = vi_resize(*b, w, vi_signed(ty));
        } else
            vi_fail(bn, "%s compares %d bits with %d bits; make them the same width", op, a->w, b->w);
    }
    a->ty = b->ty = ty;
}

static VX vi_not(VX x)
{
    if (x.cst) { VX r = vi_const(~x.v, x.w, x.ty); return r; }
    if (x.s[0] == '~' && x.atom == 1) return vi_mk(x.s + 1, x.ty, x.w, 2);   /* ~~a = a */
    return vi_mk(vi_fmt("~%s", vi_wrap(&x, 2)), x.ty, x.w, 1);
}

static VX vi_relation(gram_node *n)
{
    gram_node *ln = n->child, *op = ln->next, *rn = op->next;
    VX a, b, r;
    char o[3] = { 0, 0, 0 };
    int sgn;
    if (ln->rule == VHDL_R_aggregate) {   /* (others => '0') = x: its width comes from the other side */
        b = vi_gen(rn, -1);
        a = vi_gen(ln, b.ty == T_INT ? -1 : b.w);
    } else {
        a = vi_gen(ln, -1);
        b = vi_gen(rn, a.ty == T_INT ? -1 : a.w);
    }
    memcpy(o, op->text, op->len < 2 ? op->len : 2);
    vi_pair(&a, &b, ln, rn, o, 0);
    if (a.ty == T_INT) {
        long x = a.iv, y = b.iv, v;
        v = !strcmp(o, "=") ? x == y : !strcmp(o, "/=") ? x != y : !strcmp(o, "<") ? x < y
          : !strcmp(o, "<=") ? x <= y : !strcmp(o, ">") ? x > y : x >= y;
        return vi_const((unsigned long)v, 1, T_BOOL);
    }
    if (a.cst && b.cst) {
        long x = (long)a.v, y = (long)b.v, v;
        if (vi_signed(a.ty) && a.w < 32) {
            if (x >> (a.w - 1)) x -= 1L << a.w;
            if (y >> (a.w - 1)) y -= 1L << a.w;
        }
        v = !strcmp(o, "=") ? x == y : !strcmp(o, "/=") ? x != y : !strcmp(o, "<") ? x < y
          : !strcmp(o, "<=") ? x <= y : !strcmp(o, ">") ? x > y : x >= y;
        return vi_const((unsigned long)v, 1, T_BOOL);
    }
    if (a.w == 1 && (!strcmp(o, "=") || !strcmp(o, "/="))) {
        int eq = !strcmp(o, "=");
        VX *c = a.cst ? &a : b.cst ? &b : NULL, *s = c == &a ? &b : &a;
        if (c) r = (c->v == 1) == eq ? *s : vi_not(*s);
        else r = eq ? vi_mk(vi_fmt("xnor(%s, %s)", a.s, b.s), T_BOOL, 1, 2)
                    : vi_mk(vi_fmt("%s ^ %s", vi_wrap(&a, 1), vi_wrap(&b, 1)), T_BOOL, 1, 0);
        r.ty = T_BOOL;
        return r;
    }
    if (!strcmp(o, "<") || !strcmp(o, "<=") || !strcmp(o, ">") || !strcmp(o, ">=")) {
        if (a.ty == T_SL || a.ty == T_BOOL) vi_fail(op, "%s compares numbers; these are single bits", o);
    }
    sgn = vi_signed(a.ty) && strcmp(o, "=") && strcmp(o, "/=");
    if (sgn) {   /* signed: flip the sign bits and compare as unsigned */
        VX *p[2];
        int i;
        p[0] = &a; p[1] = &b;
        for (i = 0; i < 2; i++) {
            VX *x = p[i];
            if (x->cst) *x = vi_const(x->v ^ (1UL << (x->w - 1)), x->w, x->ty);
            else if (x->w == 1) *x = vi_not(*x);
            else *x = vi_mk(vi_fmt("{~%s[%d], %s[%d:0]}", vi_wrap(x, 2), x->w - 1, vi_wrap(x, 2), x->w - 2), x->ty, x->w, 2);
        }
    }
    {
        const char *port = !strcmp(o, "=") || !strcmp(o, "/=") ? "eq" : !strcmp(o, "<") || !strcmp(o, ">=") ? "lt" : "gt";
        int neg = !strcmp(o, "/=") || !strcmp(o, ">=") || !strcmp(o, "<=");
        r = vi_mk(vi_fmt("compare(%s, %s).%s", a.s, b.s, port), T_BOOL, 1, 2);
        if (neg) r = vi_not(r);
        r.ty = T_BOOL;
        return r;
    }
}

/* a & b, a + b, a - b, left to right */
static VX vi_simple(gram_node *n, int want)
{
    gram_node *k = n->child;
    VX acc = vi_gen(k, k->next && vi_is(k->next, "&") ? -1 : want);
    unsigned long lastv = 0;   /* the constant at the end of acc's {...}, if there is one */
    int lastw = 0;
    for (k = k->next; k; k = k->next->next) {
        gram_node *rn = k->next;
        if (vi_is(k, "&")) {
            VX b = vi_gen(rn, -1);
            vi_need_value(&acc, n->child);
            vi_need_value(&b, rn);
            if (acc.ty == T_BOOL || b.ty == T_BOOL) vi_fail(k, "& joins bits; a condition is not a bit");
            if (acc.w + b.w > MAXW) vi_fail(k, "Wire handles up to %d bits, and this is %d; wider values are not supported yet", MAXW, acc.w + b.w);
            if (acc.cst && b.cst) {
                int ty = acc.ty == T_SL || acc.ty == T_STR ? (b.ty == T_SL ? T_STR : b.ty) : acc.ty;
                acc = vi_const((acc.v << b.w) | b.v, acc.w + b.w, ty);
                continue;
            }
            if (b.cst && acc.cat && lastw && lastw + b.w <= MAXW) {   /* {x, 0b10} & "01" = {x, 0b1001} */
                size_t cut = strlen(acc.s) - strlen(vi_lit(lastv, lastw)) - 1;
                int ty = acc.ty;
                lastv = (lastv << b.w) | b.v;
                lastw += b.w;
                acc = vi_mk(vi_fmt("%.*s%s}", (int)cut, acc.s, vi_lit(lastv, lastw)), ty, acc.w + b.w, 2);
                acc.cat = 1;
                continue;
            }
            {
                int ty = vi_vec(acc.ty) && acc.ty != T_STR ? acc.ty : vi_vec(b.ty) && b.ty != T_STR ? b.ty : T_STR;
                char *s;
                lastw = b.cst ? b.w : 0;
                lastv = b.cst ? b.v : 0;
                const char *bs = b.cat ? vi_fmt("%.*s", (int)strlen(b.s) - 2, b.s + 1) : b.s;
                if (acc.cat) s = vi_fmt("%.*s, %s}", (int)strlen(acc.s) - 1, acc.s, bs);
                else s = vi_fmt("{%s, %s}", acc.s, bs);
                acc = vi_mk(s, ty, acc.w + b.w, 2);
                acc.cat = 1;
            }
        } else {
            int sub = vi_is(k, "-");
            VX b = vi_gen(rn, acc.ty == T_INT ? -1 : acc.w);
            vi_pair(&acc, &b, n->child, rn, sub ? "-" : "+", 1);
            if (acc.ty == T_INT) { acc.iv = sub ? acc.iv - b.iv : acc.iv + b.iv; acc.s = vi_fmt("%ld", acc.iv); continue; }
            if (acc.cst && b.cst) { acc = vi_const(sub ? acc.v - b.v : acc.v + b.v, acc.w, acc.ty); continue; }
            if (!sub && vi_bind_at && acc.zx && b.zx && acc.zxw == acc.w - 1 && b.zxw == acc.w - 1) {
                /* resize(a, n + 1) + b [+ c]: one adder, its carry the top bit */
                gram_node *k2 = rn->next;
                char *cin = NULL, *w;
                int ty = acc.ty;
                if (k2 && vi_is(k2, "+") && (k2->next->rule == VHDL_R_name || k2->next->rule == VHDL_R_ident)) {
                    VX c = vi_gen(k2->next, acc.w), a2 = acc;
                    vi_pair(&a2, &c, n->child, k2->next, "+", 1);
                    if (c.zx && c.zxw == 1) { cin = c.zx; k = k2; }
                    else if (c.cst && c.v <= 1) { cin = c.v ? "1" : NULL; k = k2; }
                }
                do w = vi_fmt("v%d_", ++vi_nbind); while (vi_wname_used(w));
                vi_emitp(vi_bind_at, "wire %s[%d]", w, acc.w - 1);
                vi_emitp(vi_bind_at, "%s = add(%s, %s%s%s)", w, acc.zx, b.zx, cin ? ", cin = " : "", cin ? cin : "");
                acc = vi_mk(vi_fmt("{%s.cout, %s}", w, w), ty, acc.w, 2);
                acc.cat = 1;
                acc.csum = w;
                continue;
            }
            if (!sub) acc = vi_mk(vi_fmt("add(%s, %s)", acc.s, b.s), acc.ty, acc.w, 2);
            else if (b.cst) {
                if (b.v == 0) continue;
                acc = vi_mk(vi_fmt("add(%s, %s)", acc.s, vi_lit((~b.v + 1) & vi_mask(b.w), b.w)), acc.ty, acc.w, 2);
            } else acc = vi_mk(vi_fmt("add(%s, %s, cin = 1)", acc.s, vi_not(b).s), acc.ty, acc.w, 2);
        }
    }
    return acc;
}

/* ---- inside a process: the values given so far ------------------------------ */

typedef struct { int sym; VX v; unsigned long mask; } VSlot;   /* mask: the bits given on every path */
typedef struct { VSlot *s; int n, cap; VX live; } VEnv;        /* live: 0 after exit, until the loop ends */
static VEnv *vi_env;             /* inside a process */
static int vi_clocked;           /* the process is clocked: a signal it does not set keeps its value */

static VX vi_slot_init(int si, unsigned long *mask)
{
    VSym *s = &vi_syms[si];
    VX x;
    if (vi_clocked && s->kind != VK_VAR) {   /* a register keeps its value */
        x = vi_mk(s->wname, s->ty, s->w, 2);
        x.tid = s->ut;
        *mask = vi_mask(s->w);
        return x;
    }
    x = vi_const(0, s->w, s->ty);
    x.tid = s->ut;
    *mask = 0;
    return x;
}

static VSlot *vi_slot_find(VEnv *e, int si)
{
    int i;
    for (i = 0; i < e->n; i++) if (e->s[i].sym == si) return &e->s[i];
    return NULL;
}

static VSlot *vi_slot(VEnv *e, int si)
{
    VSlot *sl = vi_slot_find(e, si);
    if (sl) return sl;
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 16;
        e->s = (VSlot *)realloc(e->s, (size_t)e->cap * sizeof *e->s);
    }
    e->s[e->n].sym = si;
    e->s[e->n].v = vi_slot_init(si, &e->s[e->n].mask);
    return &e->s[e->n++];
}

static VX vi_var_read(VSym *s, gram_node *at)
{
    VSlot *sl;
    if (!vi_env) vi_fail(at, "%.*s is a variable; it can only be read inside its process", VT(at));
    sl = vi_slot(vi_env, (int)(s - vi_syms));
    if (sl->mask != vi_mask(s->w))
        vi_fail(at, "%.*s is read before it is given a value on every path (that would need memory); give it a value first",
                VT(at));
    return sl->v;
}

/* arr(3), arr(i), arr(i)(7 downto 4): an element; picked by a signal, a mux (or a rom, for a constant) */
static VX vi_array_read(VSym *s, gram_node *id, gram_node *args, int want)
{
    VType *t = &vi_types[s->ut - 1];
    int si = (int)(s - vi_syms);
    VX e, ix;
    long k;
    (void)want;
    if (args->rule != VHDL_R_args || vi_nargs(args) != 1 || args->child->rule == VHDL_R_drange)
        vi_fail(args, "pick one element of %.*s, like %.*s(0)", VT(id), VT(id));
    if (vi_ceval(args->child, &k)) {
        if (k < t->alo || k > t->ahi) vi_fail(args->child, "%ld is outside %.*s(%d to %d)", k, VT(id), t->alo, t->ahi);
        if (s->kind == VK_CONST) e = vi_const(s->abits[k - t->alo], t->elem.w, t->elem.ty);
        else {
            VSym *el = &vi_syms[vi_syms[si].elem0 + (k - t->alo)];
            e = el->kind == VK_VAR ? vi_var_read(el, id) : vi_mk(el->wname, el->ty, el->w, 2);
        }
    } else if (s->ramproc) {   /* a ram: read at the address it is written at */
        e = vi_mk(s->wname, t->elem.ty, t->elem.w, 2);
    } else {
        int j, n;
        VBuf b = { 0, 0, 0 };
        ix = vi_gen(args->child, -1);
        if (ix.ty != T_INTV && ix.ty != T_UNS)
            vi_fail(args->child, "an index is an integer: write %.*s(to_integer(...))", VT(id));
        if (s->kind == VK_CONST) {   /* a constant array read with a signal is a rom */
            if (ix.w > 16) vi_fail(args->child, "a rom has up to 16 address bits, and this index has %d", ix.w);
            if (t->elem.w > 32) vi_fail(args->child, "a rom word has up to 32 bits");
            n = 1 << ix.w;
            vbf(&b, "rom(%s, bits = %d, data = \"", ix.s, t->elem.w);
            for (j = 0; j < n; j++) vbf(&b, "%s%lX", j ? " " : "", j >= t->alo && j <= t->ahi ? s->abits[j - t->alo] : 0UL);
            vb_add(&b, "\")", 2);
        } else {
            if (ix.w > 5) vi_fail(args->child, "picking one of more than 32 elements with a signal is not supported yet");
            n = 1 << ix.w;
            vb_add(&b, "mux(", 4);
            for (j = 0; j < n; j++) {
                if (j >= t->alo && j <= t->ahi) {
                    VSym *el = &vi_syms[vi_syms[si].elem0 + (j - t->alo)];
                    vbf(&b, "%s, ", el->wname);   /* array variables are refused when declared */
                } else vbf(&b, "%s, ", vi_lit(0, t->elem.w));
            }
            vbf(&b, "sel = %s)", ix.s);
        }
        e = vi_mk(b.s, t->elem.ty, t->elem.w, 2);
    }
    e.tid = t->elem.ut;
    if (args->next) {   /* then a bit or slice of the element */
        VSym tmp = t->elem;
        if (args->next->rule != VHDL_R_args || vi_nargs(args->next) != 1 || args->next->next)
            vi_fail(args->next, "one index or slice at a time, please");
        tmp.kind = e.cst ? VK_CONST : VK_SIG;
        tmp.bits = e.v;
        tmp.wname = vi_wrap(&e, 2);
        return vi_index(&tmp, id, args->next->child);
    }
    return e;
}

/* x shifted by a signal: one shifted copy per amount, picked with a mux */
static VX vi_barrel(VX x, VX amount, int left, int arith, int rot, gram_node *at)
{
    VBuf b = { 0, 0, 0 };
    int j, n;
    if (amount.ty == T_INT) return vi_shift(x, left, arith, rot, amount.iv, at);
    if (amount.ty != T_INTV && amount.ty != T_UNS)
        vi_fail(at, "a shift amount is an integer (to_integer(...)) or unsigned");
    if (amount.w > 5) vi_fail(at, "shifting by a signal of more than 5 bits is not supported yet");
    n = 1 << amount.w;
    vb_add(&b, "mux(", 4);
    for (j = 0; j < n; j++) vbf(&b, "%s, ", vi_shift(x, left, arith, rot, j, at).s);
    vbf(&b, "sel = %s)", amount.s);
    return vi_mk(b.s, x.ty, x.w, 2);
}

static VX vi_gen(gram_node *n, int want)
{
    VX x;
    memset(&x, 0, sizeof x);
    switch (n->rule) {
    case VHDL_R_chr:
        return vi_const((unsigned long)vi_bitchar(n, n->text[1]), 1, T_SL);
    case VHDL_R_str:
        return vi_strlit(n);
    case VHDL_R_bitstr:
        return vi_bitstr(n);
    case VHDL_R_number:
        vi_number(n, &x.iv);
        x.ty = T_INT; x.cst = 1; x.s = vi_fmt("%ld", x.iv); x.atom = 2;
        return x;
    case VHDL_R_ident: {
        VSym *s = vi_find(n);
        if (!s) {
            int t;
            if (vi_is(n, "true") || vi_is(n, "false")) return vi_const(vi_is(n, "true"), 1, T_BOOL);
            for (t = vi_ntypes - 1; t >= 0; t--) {   /* a state of an enumeration */
                gram_node *l;
                int code = 0;
                if (vi_types[t].isarr) continue;
                for (l = vi_types[t].lits; l; l = l->next, code++)
                    if (l->len == n->len && vi_is(n, vi_fmt("%.*s", VT(l)))) {
                        x = vi_const((unsigned long)code, vi_types[t].w, T_ENUM);
                        x.tid = t + 1;
                        return x;
                    }
            }
            vi_fail(n, "there is no signal, port or constant called %.*s", VT(n));
        }
        if (s->kind == VK_LABEL) vi_fail(n, "%.*s is the label of an instance, not a signal", VT(n));
        if (s->ty == T_ARR) vi_fail(n, "%.*s is an array; pick an element, like %.*s(0)", VT(n), VT(n));
        if (s->kind == VK_VAR) return vi_var_read(s, n);
        if (s->ty == T_INT) { x.ty = T_INT; x.cst = 1; x.iv = s->ival; x.s = vi_fmt("%ld", s->ival); x.atom = 2; return x; }
        if (s->kind == VK_CONST) { x = vi_const(s->bits, s->w, s->ty); x.tid = s->ut; return x; }
        x = vi_mk(s->wname, s->ty, s->w, 2);
        x.tid = s->ut;
        return x;
    }
    case VHDL_R_name:
        return vi_call(n, want);
    case VHDL_R_aggregate:
        return vi_aggregate(n, want);
    case VHDL_R_notx:
        x = vi_gen(n->child, want);
        vi_need_value(&x, n->child);
        return vi_not(x);
    case VHDL_R_neg:
        x = vi_gen(n->child, want);
        if (x.ty == T_INT) { x.iv = -x.iv; x.s = vi_fmt("%ld", x.iv); return x; }
        if (!vi_signed(x.ty)) vi_fail(n, "only signed values can be negated, and this is %s", vi_tyname[x.ty]);
        if (x.cst) return vi_const(~x.v + 1, x.w, x.ty);
        return vi_mk(vi_fmt("add(%s, %s, cin = 1)", vi_not(x).s, vi_lit(0, x.w)), x.ty, x.w, 2);
    case VHDL_R_absx:
        vi_fail(n, "abs is not supported yet");
        break;
    case VHDL_R_expr:
        return vi_logic(n, want);
    case VHDL_R_relation:
        return vi_relation(n);
    case VHDL_R_shift: {
        gram_node *op = n->child->next;
        long k;
        x = vi_gen(n->child, want);
        if (!vi_vec(x.ty) || x.ty == T_STR) vi_fail(n->child, "%.*s shifts a vector", VT(op));
        if (!vi_ceval(op->next, &k))   /* by a signal: a barrel shifter */
            return vi_barrel(x, vi_gen(op->next, -1), vi_is(op, "sll") || vi_is(op, "sla") || vi_is(op, "rol"),
                             vi_is(op, "sra") || vi_is(op, "sla"), vi_is(op, "rol") || vi_is(op, "ror"), n);
        return vi_shift(x, vi_is(op, "sll") || vi_is(op, "sla") || vi_is(op, "rol"), vi_is(op, "sra") || vi_is(op, "sla"),
                        vi_is(op, "rol") || vi_is(op, "ror"), k, n);
    }
    case VHDL_R_simple:
        return vi_simple(n, want);
    case VHDL_R_term:
        if (vi_ceval(n, &x.iv)) { x.ty = T_INT; x.cst = 1; x.s = vi_fmt("%ld", x.iv); x.atom = 2; return x; }
        vi_fail(n->child->next, "%.*s on signals is not supported yet (only on constant integers)", VT(n->child->next));
        break;
    default:
        vi_fail(n, "this kind of expression is not supported yet");
    }
    return x;
}

/* ---- statements -------------------------------------------------------------- */

static void vi_inst(gram_node *st);
static void vi_generate(gram_node *st);
static void vi_scan_clocks(gram_node *first);
static void vi_scan_regs(gram_node *first);
static int vi_is_reg(const gram_node *id);
static gram_node *vi_regs[256];
static int vi_nregs;
static gram_node *vi_clk[64];
static int vi_nclk;
static VEnt *vi_copy(VEnt *e, long *val, gram_node *at);
static void vi_decls(gram_node *first);
static void vi_ports(gram_node *ports, int is_generic, int emit);

typedef struct { char *s; int ty, w; VSym *sym; int ut; } VTarget;

static VTarget vi_target(gram_node *t)
{
    VTarget r;
    gram_node *id = t->child;
    VSym *s = vi_find(id);
    if (!s) vi_fail(id, "there is no signal or port called %.*s", VT(id));
    if (s->kind == VK_IN) vi_fail(id, "%.*s is an input of the entity, so it cannot be given a value here", VT(id));
    if (s->kind == VK_CONST || s->kind == VK_GEN) vi_fail(id, "%.*s is a constant", VT(id));
    if (s->kind == VK_LABEL) vi_fail(id, "%.*s is the label of an instance, not a signal", VT(id));
    if (s->kind == VK_VAR) vi_fail(id, "%.*s is a variable; it is given a value with := inside a process", VT(id));
    r.sym = s;
    r.ut = s->ut;
    if (s->ty == T_ARR) {   /* an element: arr(3), arr(3)(7 downto 4) */
        VType *t = &vi_types[s->ut - 1];
        long k;
        if (!id->next) vi_fail(id, "%.*s is an array; give its elements values one by one, or in a process", VT(id));
        if (!vi_ceval(id->next, &k))
            vi_fail(id->next, "an element picked by a signal can only be given a value in a clocked process");
        if (k < t->alo || k > t->ahi) vi_fail(id->next, "%ld is outside %.*s(%d to %d)", k, VT(id), t->alo, t->ahi);
        s = &vi_syms[s->elem0 + (k - t->alo)];
        r.sym = s;
        r.ut = s->ut;
        if (!id->next->next) { r.s = s->wname; r.ty = s->ty; r.w = s->w; return r; }
        {
            VX x = vi_index(s, id, id->next->next);
            r.s = x.s; r.ty = x.ty; r.w = x.w;
        }
        return r;
    }
    if (!id->next) { r.s = s->wname; r.ty = s->ty; r.w = s->w; return r; }
    if (id->next->next) vi_fail(id->next->next, "one index or slice at a time, please");
    {
        VX x = vi_index(s, id, id->next);
        r.s = x.s; r.ty = x.ty; r.w = x.w;
    }
    return r;
}

/* can value x be given to the target? */
static void vi_assignable(const VTarget *t, VX *x, gram_node *at)
{
    if (t->ty == T_ENUM) {
        if (x->ty != T_ENUM || x->tid != t->ut)
            vi_fail(at, "this target is a state of %s, so give it one of its states", vi_fmt("%.*s", (int)vi_types[t->ut - 1].id->len, vi_types[t->ut - 1].id->text));
        return;
    }
    if (x->ty == T_ENUM) vi_fail(at, "this is a state, but the target is %s", vi_tyname[t->ty]);
    if (t->ty == T_INTV) {
        if (x->ty == T_INT) { *x = vi_int_as(x, t->w, T_INTV, at); return; }
        if (x->ty == T_INTV) { *x = vi_resize(*x, t->w, 0); x->ty = T_INTV; return; }
        vi_fail(at, "this target is an integer, but the value is %s: convert it with to_integer(...)", vi_tyname[x->ty]);
    }
    if (x->ty == T_INTV) vi_fail(at, "this is an integer, but the target is %s: convert it with to_unsigned(x, %d)", vi_tyname[t->ty], t->w);
    if (x->ty == T_INT) {
        if (t->ty == T_SL) vi_fail(at, "%s is one bit: give it '0' or '1'", t->s);
        vi_fail(at, "an integer is not a %s: write to_unsigned(%ld, %d)%s", vi_tyname[t->ty], x->iv, t->w,
                t->ty == T_SLV ? " inside std_logic_vector(...)" : "");
    }
    if (t->ty == T_BOOL) {
        if (x->ty != T_BOOL) vi_fail(at, "this target is a boolean, so give it a condition (a comparison, true or false)");
        return;
    }
    if (t->ty == T_SL) {
        if (x->ty == T_BOOL) vi_fail(at, "this is a condition (true or false), not a bit: write '1' when ... else '0'");
        if (x->ty != T_SL) vi_fail(at, "this target is one bit (std_logic), but the value is %s", vi_desc(x));
        return;
    }
    if (x->ty == T_SL || x->ty == T_BOOL)
        vi_fail(at, "this target is a vector of %d bit%s, but the value is %s", t->w, t->w == 1 ? "" : "s", vi_desc(x));
    if (x->ty != T_STR && x->ty != t->ty)
        vi_fail(at, "the target is %s but the value is %s: convert it with %s(...)", vi_tyname[t->ty],
                vi_tyname[x->ty], vi_tyname[t->ty]);
    if (x->w != t->w)
        vi_fail(at, "the target is %d bit%s, but the value is %d bit%s", t->w, t->w == 1 ? "" : "s", x->w, x->w == 1 ? "" : "s");
}

static VX vi_cond(gram_node *c)
{
    VX x = vi_gen(c, 1);
    if (x.ty == T_INT || x.w != 1) vi_fail(c, "a condition is a comparison or a single bit, but this is %s", vi_desc(&x));
    return x;
}

static VX vi_value(gram_node *e, const VTarget *t)
{
    gram_node *v = e->rule == VHDL_R_wave ? e->child : e;
    VX x = vi_gen(v, t->w);
    vi_assignable(t, &x, v);
    return x;
}

/* mux(d0, d1, sel = c): d1 when c */
static VX vi_mux2(VX d0, VX d1, VX c)
{
    VX r;
    if (d0.s && d1.s && !strcmp(d0.s, d1.s)) return d0;
    if (c.cst) return c.v ? d1 : d0;
    if (d0.w == 1 && d0.cst && d1.cst) {         /* '1' when c else '0' is c */
        if (d1.v == 1 && d0.v == 0) { r = c; r.ty = d0.ty == T_STR ? T_SL : d0.ty; r.tid = d0.tid; r.m0 = r.m1 = r.ms = NULL; return r; }
        if (d1.v == 0 && d0.v == 1) { r = vi_not(c); r.ty = d0.ty == T_STR ? T_SL : d0.ty; r.tid = d0.tid; return r; }
    }
    r = vi_mk(vi_fmt("mux(%s, %s, sel = %s)", d0.s, d1.s, c.s), d1.ty == T_STR ? d0.ty : d1.ty, d0.w, 2);
    r.tid = d0.tid ? d0.tid : d1.tid;
    r.m0 = d0.s; r.m1 = d1.s; r.ms = c.s;
    return r;
}

/* a long value used more than once gets a wire of its own, so values that
 * build on each other (a loop, an if after an if) do not grow without end */

static void vi_bind(VX *x)
{
    char *w;
    if (!vi_bind_at || x->cst || !x->s || x->w < 1 || x->ty == T_INT || strlen(x->s) < 120) return;
    do w = vi_fmt("v%d_", ++vi_nbind); while (vi_wname_used(w));
    vi_emitp(vi_bind_at, "wire %s[%d]", w, x->w);
    vi_emitp(vi_bind_at, "%s = %s", w, x->s);
    x->s = w;
    x->atom = 2;
    x->cat = 0;
    x->m0 = x->m1 = x->ms = NULL;
    x->zx = x->csum = NULL;
}

static void vi_condassign(gram_node *st)
{
    gram_node *k = st->child, *tn, *first;
    VTarget t;
    VX acc, vals[256], conds[256];
    int n = 0, i;
    if (k->rule == VHDL_R_label) k = k->next;
    tn = k;
    t = vi_target(tn);
    first = tn->next;
    for (k = first->next; k; k = k->next) {
        if (k->rule == VHDL_R_lastwhen)
            vi_fail(k, "this when has no else: the value would have to be kept, which is a latch. Add else ...");
    }
    vals[0] = vi_value(first, &t);
    for (k = first->next; k; k = k->next) {
        if (n == 255) vi_fail(k, "too many whens");
        conds[n] = vi_cond(k->child);
        vals[++n] = vi_value(k->child->next, &t);
    }
    /* a when c0 else b when c1 else d  =  mux(mux(d, b, c1), a, c0) */
    acc = vals[n];
    for (i = n - 1; i >= 0; i--) acc = vi_mux2(acc, vals[i], conds[i]);
    vi_emit(tn, "%s = %s", t.s, acc.s);
}

static void vi_selassign(gram_node *st)
{
    gram_node *k = st->child, *sn, *tn, *arm, *others = NULL;
    VTarget t;
    VX sel, *tab = NULL, oth;
    unsigned long *chosen = NULL;
    int k2, i, n, nchosen = 0;
    VBuf b = { 0, 0, 0 };
    if (k->rule == VHDL_R_label) k = k->next;
    sn = k;
    tn = sn->next;
    sel = vi_gen(sn, -1);
    if (sel.ty == T_INT) vi_fail(sn, "integer signals are not supported yet; select on a vector");
    k2 = sel.w;
    t = vi_target(tn);
    memset(&oth, 0, sizeof oth);
    if (k2 <= 5) tab = (VX *)calloc((size_t)1 << k2, sizeof *tab);
    for (arm = tn->next; arm; arm = arm->next) {
        VX v = vi_value(arm->child, &t);
        gram_node *c;
        for (c = arm->child->next->child; c; c = c->next) {
            VX cv;
            if (c->rule == VHDL_R_OTHERS) {
                if (others) vi_fail(c, "others is given twice");
                if (c->next || c != arm->child->next->child) vi_fail(c, "others goes alone, in the last when");
                others = c;
                oth = v;
                continue;
            }
            if (others) vi_fail(c, "when others must be the last choice");
            cv = vi_gen(c, k2);
            if (cv.ty == T_INT) cv = vi_int_as(&cv, k2, vi_num(sel.ty) ? sel.ty : T_UNS, c);
            if (!cv.cst) vi_fail(c, "each choice must be a constant, like \"01\"");
            if (cv.w != k2) vi_fail(c, "the choice is %d bit%s, but %.*s is %d", cv.w, cv.w == 1 ? "" : "s", VT(sn), k2);
            for (i = 0; i < nchosen; i++) if (chosen[i] == cv.v) vi_fail(c, "this value is chosen twice");
            chosen = (unsigned long *)realloc(chosen, (size_t)(nchosen + 1) * sizeof *chosen);
            chosen[nchosen++] = cv.v;
            if (tab) tab[cv.v] = v;   /* too wide for one mux: done below, as comparisons */
        }
    }
    if (k2 <= 5) {
        n = 1 << k2;
        for (i = 0; i < n; i++)
            if (!tab[i].s) {
                if (!others) {
                    char miss[40];
                    int j;
                    for (j = 0; j < k2; j++) miss[j] = (char)('0' + ((i >> (k2 - 1 - j)) & 1));
                    miss[k2] = 0;
                    vi_fail(st, "with ... select must cover every value of %.*s (\"%s\" is missing); add when others",
                            VT(sn), miss);
                }
                tab[i] = oth;
            }
        vb_add(&b, "mux(", 4);
        for (i = 0; i < n; i++) vbf(&b, "%s, ", tab[i].s);
        vbf(&b, "sel = %s)", sel.s);
        vi_emit(tn, "%s = %s", t.s, b.s);
        return;
    }
    /* wide select: when others, then each arm in turn */
    if (!others) vi_fail(st, "%.*s has %d bits, too many to list every value; add when others", VT(sn), k2);
    {
        VX acc = oth;
        for (arm = tn->next; arm; arm = arm->next) {
            VX v = vi_value(arm->child, &t), cond;
            gram_node *c;
            VBuf cb = { 0, 0, 0 };
            int m = 0;
            if (arm->child->next->child->rule == VHDL_R_OTHERS) continue;
            for (c = arm->child->next->child; c; c = c->next) {
                VX cv = vi_gen(c, k2);
                if (cv.ty == T_INT) cv = vi_int_as(&cv, k2, vi_num(sel.ty) ? sel.ty : T_UNS, c);
                vbf(&cb, "%scompare(%s, %s).eq", m++ ? " | " : "", sel.s, cv.s);
            }
            cond = vi_mk(cb.s, T_BOOL, 1, m > 1 ? 0 : 2);
            if (m > 1) cond = vi_mk(vi_fmt("(%s)", cb.s), T_BOOL, 1, 2);
            acc = vi_mux2(acc, v, cond);
        }
        vi_emit(tn, "%s = %s", t.s, acc.s);
    }
}

static VEnt *vi_ent_named(const gram_node *n)   /* work.full_adder or full_adder */
{
    const char *p = n->text, *e = n->text + n->len, *dot;
    int i;
    for (dot = p; dot < e; dot++) if (*dot == '.') p = dot + 1;
    for (i = 0; i < vi_nbase; i++) {
        const gram_node *id = vi_ents[i]->ent->child;
        size_t j;
        if (id->len != (size_t)(e - p)) continue;
        for (j = 0; j < id->len; j++) if (vi_lower(id->text[j]) != vi_lower(p[j])) break;
        if (j == id->len) return vi_ents[i];
    }
    return NULL;
}

static gram_node *vi_inst_name(gram_node *st)   /* the entity an instance places */
{
    return st->child->next;
}

/* the port's width and type, for a value or a target */
static void vi_port_fits(const VSym *p, const VEnt *e, int ty, int w, gram_node *at, int is_out)
{
    const char *en = vi_fmt("%.*s", VT(e->ent->child));
    if (ty == T_INT) vi_fail(at, "port %.*s of %s is %s; an integer cannot go there", p->nlen, p->name, en, vi_tyname[p->ty]);
    if ((p->ty == T_SL || p->ty == T_BOOL) != (ty == T_SL || ty == T_BOOL))
        vi_fail(at, "port %.*s of %s is %s, but this is %s", p->nlen, p->name, en,
                p->ty == T_SL || p->ty == T_BOOL ? "1 bit (std_logic)" : vi_fmt("a vector of %d bits", p->w),
                ty == T_SL || ty == T_BOOL ? "1 bit (std_logic)" : vi_fmt("a vector of %d bits", w));
    if (w != p->w)
        vi_fail(at, "port %.*s of %s is %d bit%s, but this is %d bit%s", p->nlen, p->name, en,
                p->w, p->w == 1 ? "" : "s", w, w == 1 ? "" : "s");
    if (ty != T_STR && vi_vec(ty) && vi_vec(p->ty) && ty != p->ty)
        vi_fail(at, "port %.*s of %s is %s, but this is %s: convert it with %s(...)", p->nlen, p->name, en,
                vi_tyname[p->ty], vi_tyname[ty], is_out ? vi_tyname[ty] : vi_tyname[p->ty]);
}

/* fa: entity work.full_adder port map(a => x, b => y, cin => c, s => s0, cout => c1);
 * becomes  fa = full_adder(a = x, b = y, cin = c); s0 = fa.s; c1 = fa.cout */
static void vi_inst(gram_node *st)
{
    gram_node *label = st->child->child, *en = vi_inst_name(st), *pm = vi_kid(st, VHDL_R_pmap), *gm = vi_kid(st, VHDL_R_gmap), *as;
    VEnt *e = vi_ent_named(en);
    VSym *inst;
    gram_node **actual;
    int i, pos = 0, named = 0;
    VBuf call = { 0, 0, 0 }, outs = { 0, 0, 0 };
    if (!e) vi_fail(en, "there is no entity called %.*s in this file", VT(en));
    if (e == vi_cur) vi_fail(en, "%.*s cannot be used inside itself", VT(e->ent->child));
    if (gm) {   /* other values than the defaults: a copy of the entity made for them */
        long *val = (long *)calloc((size_t)e->ngens + 1, sizeof *val);
        int differ = 0;
        for (i = 0; i < e->ngens; i++) val[i] = e->gens[i].ival;
        for (as = gm->child; as; as = as->next) {
            gram_node *f = as->child->next ? as->child : NULL, *v = f ? f->next : as->child;
            int g = -1;
            if (f) {
                for (i = 0; i < e->ngens; i++)
                    if (e->gens[i].nlen == (int)f->child->len && vi_is(f->child, vi_fmt("%.*s", e->gens[i].nlen, e->gens[i].name)))
                        g = i;
                if (g < 0) vi_fail(f, "%.*s has no generic called %.*s", VT(e->ent->child), VT(f->child));
            } else {
                if (pos >= e->ngens) vi_fail(v, "%.*s has only %d generics", VT(e->ent->child), e->ngens);
                g = pos++;
            }
            if (v->rule == VHDL_R_OPEN) continue;
            if (!vi_ceval(v, &val[g])) vi_fail(v, "a generic's value must be a constant");
            if (val[g] != e->gens[g].ival) differ = 1;
        }
        pos = 0;
        if (differ) e = vi_copy(e, val, st);
    }
    actual = (gram_node **)calloc((size_t)e->nports + 1, sizeof *actual);
    for (as = pm->child; as; as = as->next) {
        gram_node *f = as->child->next ? as->child : NULL, *v = f ? f->next : as->child;
        if (f) {
            named = 1;
            if (f->child->next) vi_fail(f, "connecting part of a port (%.*s(...) =>) is not supported yet", VT(f->child));
            for (i = 0; i < e->nports; i++)
                if (e->ports[i].nlen == (int)f->child->len && vi_is(f->child, vi_fmt("%.*s", e->ports[i].nlen, e->ports[i].name))) break;
            if (i == e->nports) vi_fail(f, "%.*s has no port called %.*s", VT(e->ent->child), VT(f->child));
        } else {
            if (named) vi_fail(v, "put the ports without a name (positional) before the named ones");
            if (pos >= e->nports) vi_fail(v, "%.*s has only %d ports", VT(e->ent->child), e->nports);
            i = pos++;
        }
        if (actual[i]) vi_fail(as, "port %.*s is connected twice", e->ports[i].nlen, e->ports[i].name);
        actual[i] = v;
    }
    inst = vi_declare(label, VK_LABEL);
    vbf(&call, "%s = %s(", inst->wname, e->wname);
    for (i = 0; i < e->nports; i++) {
        VSym *p = &e->ports[i];
        gram_node *v = actual[i];
        if (p->kind == VK_IN) {
            VX x;
            if (!v || v->rule == VHDL_R_OPEN) {
                if (!p->def) vi_fail(st, "input %.*s of %.*s is not connected (and has no default value)", p->nlen, p->name, VT(e->ent->child));
                x = vi_const(p->bits, p->w, p->ty == T_SL ? T_SL : T_STR);   /* worked out in the entity */
            } else x = vi_gen(v, p->w);
            vi_port_fits(p, e, x.ty, x.w, v ? v : st, 0);
            vbf(&call, "%s%s = %s", call.s[call.n - 1] == '(' ? "" : ", ", p->wname, x.s);
        } else {
            VTarget t;
            if (!v || v->rule == VHDL_R_OPEN) continue;
            if (v->rule != VHDL_R_ident && v->rule != VHDL_R_name)
                vi_fail(v, "output %.*s of %.*s must go to a signal (or open)", p->nlen, p->name, VT(e->ent->child));
            {   /* the signal as a target: x, x(3), x(7 downto 4) */
                gram_node *id = v->rule == VHDL_R_ident ? v : v->child;
                VSym *s = vi_find(id);
                if (!s || s->kind > VK_SIG) vi_fail(id, "%.*s is not a signal or port", VT(id));
                if (s->kind == VK_IN) vi_fail(id, "%.*s is an input, so it cannot be driven by %.*s", VT(id), VT(label));
                if (v->rule == VHDL_R_name) {
                    gram_node *args = id->next;
                    if (args->rule != VHDL_R_args || args->next || vi_nargs(args) != 1)
                        vi_fail(v, "output %.*s of %.*s must go to a signal, a bit or a slice", p->nlen, p->name, VT(e->ent->child));
                    {
                        VX x = vi_index(s, id, args->child);
                        t.s = x.s; t.ty = x.ty; t.w = x.w;
                    }
                } else { t.s = s->wname; t.ty = s->ty; t.w = s->w; }
            }
            vi_port_fits(p, e, t.ty, t.w, v, 1);
            vbf(&outs, "; %s = %s.%s", t.s, inst->wname, p->wname);
        }
    }
    vb_add(&call, ")", 1);
    vi_emit(label, "%s%s", call.s, outs.n ? outs.s : "");
}

static void vi_statement(gram_node *st);

/* an entity's ports and generics, as instances see them (with vi_over's values, for a copy) */
static void vi_set_ctx(int ctx)
{
    vi_ctx = ctx;
    vi_slv_num = ctx & CTX_SGN ? 2 : ctx & CTX_UNS ? 1 : 0;
}

static void vi_ent_ports(VEnt *e)
{
    int k;
    const long *saved = vi_over;
    int savectx = vi_ctx, savetypes = vi_ntypes;
    vi_set_ctx(e->ctx);
    vi_ntypes = 0;   /* the architecture's types are not seen by the ports */
    vi_over = e->over;
    vi_nsyms = 0;
    vi_ports(vi_kid(e->ent, VHDL_R_generics), 1, 0);
    vi_ports(vi_kid(e->ent, VHDL_R_ports), 0, 0);
    e->nports = e->ngens = 0;
    for (k = 0; k < vi_nsyms; k++) if (vi_syms[k].kind == VK_IN || vi_syms[k].kind == VK_OUT) e->nports++;
    e->gens = (VSym *)calloc((size_t)vi_nsyms + 1, sizeof *e->gens);
    for (k = 0; k < vi_nsyms; k++) if (vi_syms[k].kind == VK_GEN) e->gens[e->ngens++] = vi_syms[k];
    e->ports = (VSym *)calloc((size_t)e->nports + 1, sizeof *e->ports);
    e->nports = 0;
    for (k = 0; k < vi_nsyms; k++)
        if (vi_syms[k].kind == VK_IN || vi_syms[k].kind == VK_OUT) {
            VSym *p = &e->ports[e->nports++];
            *p = vi_syms[k];
            if ((int)strlen(p->wname) >= NAMEW && !e->top)
                vi_fail(p->at, "port names of an entity used as a chip are at most %d characters", NAMEW - 1);
            if (p->def) {   /* a default is a constant of the entity: work it out here */
                VX x = vi_gen(p->def, p->w);
                if (!x.cst) vi_fail(p->def, "a port's default must be a constant");
                p->bits = x.v;
            }
        }
    if (!e->top && !e->nports) vi_fail(e->ent->child, "%.*s has no ports, so it cannot be used as a chip", VT(e->ent->child));
    vi_over = saved;
    vi_set_ctx(savectx);
    vi_ntypes = savetypes;
}

static int vi_wname_taken(const char *w)   /* a component name already in use (or a part of Wire) */
{
    static const char *parts[] = { "and", "or", "nand", "nor", "xor", "xnor", "not", "buf", "mux", "demux", "decoder",
                                   "encoder", "add", "compare", "dff", "reg", "tff", "jkff", "counter", "tri", "rom",
                                   "ram", NULL };
    int i;
    for (i = 0; parts[i]; i++) if (!strcmp(parts[i], w)) return 1;
    if (vi_wire_word(w, (int)strlen(w))) return 1;
    for (i = 0; i < vi_nents; i++) if (vi_ents[i]->wname && !strcmp(vi_ents[i]->wname, w)) return 1;
    return 0;
}

static char *vi_unique(char *w)
{
    while (vi_wname_taken(w)) w = vi_fmt("%s_", w);
    return w;
}

/* e with other generics: one copy per set of values, named like add_w_carry_n8 */
static VEnt *vi_copy(VEnt *e, long *val, gram_node *at)
{
    VEnt *c;
    int i, k, nsave = vi_nsyms;
    VSym *saved;
    char suf[sizeof vi_suffix];
    VBuf name = { 0, 0, 0 };
    for (i = vi_nbase; i < vi_nents; i++) {
        c = vi_ents[i];
        if (c->ent != e->ent) continue;
        for (k = 0; k < e->ngens && c->over[k] == val[k]; k++) {}
        if (k == e->ngens) return c;
    }
    if (vi_nents - vi_nbase >= 64) vi_fail(at, "too many different generic maps (at most 64)");
    c = (VEnt *)calloc(1, sizeof *c);
    c->ent = e->ent; c->arch = e->arch; c->used = 1; c->over = val; c->ctx = e->ctx;
    vbf(&name, "%.*s", VT(e->ent->child));
    for (k = 0; k < e->ngens; k++)
        if (val[k] != e->gens[k].ival) {
            int j;
            vb_add(&name, "_", 1);
            for (j = 0; j < e->gens[k].nlen; j++) { char ch = (char)vi_lower(e->gens[k].name[j]); vb_add(&name, &ch, 1); }
            vbf(&name, "%s%ld", val[k] < 0 ? "m" : "", val[k] < 0 ? -val[k] : val[k]);
        }
    c->wname = vi_unique(name.s);
    vi_ents = (VEnt **)realloc(vi_ents, (size_t)(vi_nents + 1) * sizeof *vi_ents);
    vi_ents[vi_nents++] = c;
    /* its ports, without losing the names of the entity being read now */
    saved = (VSym *)malloc((size_t)(nsave + 1) * sizeof *saved);
    memcpy(saved, vi_syms, (size_t)nsave * sizeof *saved);
    memcpy(suf, vi_suffix, sizeof suf);
    vi_suffix[0] = 0;
    vi_ent_ports(c);
    memcpy(vi_syms, saved, (size_t)nsave * sizeof *saved);
    vi_nsyms = nsave;
    memcpy(vi_suffix, suf, sizeof suf);
    free(saved);
    return c;
}

/* one entity as Wire: the top as the circuit, the others as components */
static void vi_ent_emit(VEnt *e)
{
    gram_node *k;
    vi_cur = e;
    vi_set_ctx(e->ctx);
    vi_over = e->over;
    vi_to_extra = e->over != NULL;
    vi_nsyms = 0;
    vi_ntypes = 0;
    vi_nfuncs = 0;
    vi_suffix[0] = 0;
    vi_nclk = 0;
    if (e->top) vi_scan_clocks(e->arch->child->next->next);   /* their ports become clock */
    if (e->top) vi_emit(e->ent, "circuit \"%.*s\"", VT(e->ent->child));
    vi_ports(vi_kid(e->ent, VHDL_R_generics), 1, 0);
    vi_ports(vi_kid(e->ent, VHDL_R_ports), 0, e->top);
    if (!e->top) {
        VBuf h = { 0, 0, 0 };
        int j, o;
        vbf(&h, "component %s(", e->wname);
        for (o = 0; o < 2; o++) {
            int first = 1;
            if (o) vb_add(&h, ") -> (", 6);
            for (j = 0; j < e->nports; j++) {
                VSym *p = &e->ports[j];
                if ((p->kind == VK_OUT) != o) continue;
                vbf(&h, "%s%s", first ? "" : ", ", p->wname);
                if (vi_vec(p->ty)) vbf(&h, "[%d]", p->w);
                first = 0;
            }
        }
        vb_add(&h, ") {", 3);
        vi_emit(e->ent, "%s", h.s);
    }
    vi_decls(e->arch->child->next->next);
    for (k = e->arch->child->next->next; k; k = k->next) {
        if (k->rule == VHDL_R_signal || k->rule == VHDL_R_constant || k->rule == VHDL_R_compdecl
            || k->rule == VHDL_R_other || k->rule == VHDL_R_ident || k->rule == VHDL_R_typedecl || k->rule == VHDL_R_funcdecl)
            continue;
        vi_statement(k);
    }
    if (!e->top) vi_emitp(e->arch->text + e->arch->len - 1, "}");
    e->done = 1;
    vi_over = NULL;
    vi_to_extra = 0;
}

/* instances inside generates count too */
static void vi_mark_used(gram_node *first)
{
    gram_node *k;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_inst) {
            VEnt *e = vi_ent_named(vi_inst_name(k));
            if (e) e->used = 1;
        } else if (k->rule == VHDL_R_generate) {
            gram_node *g = k->child->next, *b = g->child->next;
            if (g->rule == VHDL_R_forgen) b = b->next;
            if (b && b->rule == VHDL_R_gdecls) b = b->next;
            vi_mark_used(b);
        }
    }
}

/* the statements of a generate, once for each value (for) or once or not at all (if) */
static void vi_generate(gram_node *st)
{
    gram_node *g = st->child->next, *body, *k;
    long from, to, i, step;
    char saved[sizeof vi_suffix];
    if (g->rule == VHDL_R_ifgen) {
        VX c = vi_gen(g->child, 1);
        if (!c.cst || c.w != 1) vi_fail(g->child, "the condition of if ... generate must be a constant");
        if (!c.v) return;
        from = to = 0; step = 1; body = g->child->next;
    } else {
        gram_node *r = g->child->next;
        from = vi_const_int(r->child, "the range of a generate");
        to = vi_const_int(r->child->next->next, "the range of a generate");
        step = vi_is(r->child->next, "downto") ? -1 : 1;
        if ((to - from) * step < 0) return;   /* an empty range */
        if ((to - from) * step > 1023) vi_fail(r, "this generate makes more than 1024 copies");
        body = r->next;
    }
    memcpy(saved, vi_suffix, sizeof saved);
    for (i = from; ; i += step) {
        int mark = vi_nsyms;
        if (g->rule == VHDL_R_forgen) {
            VSym *v;
            size_t n = strlen(saved);
            snprintf(vi_suffix + n, sizeof vi_suffix - n, "_%ld", i < 0 ? -i : i);
            v = vi_declare(g->child, VK_CONST);
            v->ty = T_INT;
            v->ival = i;
        }
        k = body;
        if (k && k->rule == VHDL_R_gdecls) { vi_decls(k->child); k = k->next; }
        for (; k; k = k->next) if (k->rule != VHDL_R_ident) vi_statement(k);
        vi_nsyms = mark;   /* the loop's names go out of scope */
        memcpy(vi_suffix, saved, sizeof saved);
        if (i == to) break;
    }
}


/* ---- processes ------------------------------------------------------------------ */

static VEnv vi_env_copy(const VEnv *e)
{
    VEnv c = *e;
    c.cap = e->n ? e->n : 1;
    c.s = (VSlot *)malloc((size_t)c.cap * sizeof *c.s);
    memcpy(c.s, e->s, (size_t)e->n * sizeof *c.s);
    return c;
}

static int vi_live(const VEnv *e) { return e->live.cst && e->live.v == 1; }   /* no exit has happened */

/* the bits hi..lo of old replaced by v */
static VX vi_splice(VX old, int w, int hi, int lo, VX v)
{
    VBuf b = { 0, 0, 0 };
    VX r;
    if (hi == w - 1 && lo == 0) return v;
    if (old.cst && v.cst) {
        unsigned long m = vi_mask(hi - lo + 1) << lo;
        r = vi_const((old.v & ~m) | ((v.v << lo) & m), w, old.ty);
        r.tid = old.tid;
        return r;
    }
    vb_add(&b, "{", 1);
    if (hi < w - 1)
        vbf(&b, "%s, ", old.cst ? vi_lit(old.v >> (hi + 1), w - 1 - hi) : vi_sl(vi_wrap(&old, 2), w - 1, hi + 1));
    vbf(&b, "%s", v.s);
    if (lo > 0) vbf(&b, ", %s", old.cst ? vi_lit(old.v, lo) : vi_sl(vi_wrap(&old, 2), lo - 1, 0));
    vb_add(&b, "}", 1);
    r = vi_mk(b.s, old.ty, w, 2);
    r.cat = 1;
    r.tid = old.tid;
    return r;
}

/* si (bits hi..lo, or all when lo < 0) gets v, on this path */
static void vi_put(VEnv *e, int si, VX v, int hi, int lo)
{
    VSlot *sl = vi_slot(e, si);
    VSym *s = &vi_syms[si];
    VX nv = lo < 0 ? v : vi_splice(sl->v, s->w, hi, lo, v);
    unsigned long bits = lo < 0 ? vi_mask(s->w) : vi_mask(hi - lo + 1) << lo;
    if (!vi_live(e)) { vi_bind(&sl->v); vi_bind(&nv); nv = vi_mux2(sl->v, nv, e->live); bits = 0; }
    nv.tid = s->ut;
    if (nv.ty == T_STR) nv.ty = s->ty;
    sl->v = nv;
    sl->mask |= bits;
}

/* si gets v only when c: an element picked by a signal */
static void vi_put_when(VEnv *e, int si, VX v, VX c)
{
    VSlot *sl = vi_slot(e, si);
    VX nv;
    vi_bind(&sl->v);
    nv = vi_mux2(sl->v, v, c);
    if (!vi_live(e)) nv = vi_mux2(sl->v, nv, e->live);
    nv.tid = vi_syms[si].ut;
    sl->v = nv;
}

/* after if c then t else f: each value is mux(f, t, sel = c) */
static void vi_merge(VEnv *out, VEnv *t, VEnv *f, VX c)
{
    VEnv m;
    int i, pass;
    memset(&m, 0, sizeof m);
    for (pass = 0; pass < 2; pass++) {
        VEnv *src = pass ? f : t;
        for (i = 0; i < src->n; i++) {
            int si = src->s[i].sym;
            VSlot *a, *b, *o;
            unsigned long ma, mb;
            VX va, vb;
            if (vi_slot_find(&m, si)) continue;
            a = vi_slot_find(t, si); b = vi_slot_find(f, si);
            if (a) { va = a->v; ma = a->mask; } else va = vi_slot_init(si, &ma);
            if (b) { vb = b->v; mb = b->mask; } else vb = vi_slot_init(si, &mb);
            o = vi_slot(&m, si);
            if (strcmp(va.s, vb.s)) { vi_bind(&va); vi_bind(&vb); }
            o->v = vi_mux2(vb, va, c);
            o->v.tid = vi_syms[si].ut;
            o->mask = ma & mb;
        }
    }
    m.live = vi_mux2(f->live, t->live, c);
    *out = m;
}

static VX vi_eq_code(VX sel, unsigned long code)   /* sel = code, as a condition */
{
    VX r;
    if (sel.w == 1) { r = code ? sel : vi_not(sel); r.ty = T_BOOL; return r; }
    return vi_mk(vi_fmt("compare(%s, %s).eq", sel.s, vi_lit(code, sel.w)), T_BOOL, 1, 2);
}

static void vi_seq(gram_node *first);

/* element i of an array value: another array, or an aggregate like (others => (others => '0')) */
static VX vi_array_elem(VType *t, gram_node *v, int i, gram_node *at)
{
    long idx = t->alo + i;
    VX x;
    VTarget tt;
    memset(&tt, 0, sizeof tt);
    tt.s = "an element"; tt.ty = t->elem.ty; tt.w = t->elem.w; tt.ut = t->elem.ut;
    if (v->rule == VHDL_R_ident) {
        VSym *a = vi_find(v);
        if (!a || a->ty != T_ARR || &vi_types[a->ut - 1] != t) vi_fail(v, "this is not an array of the same type");
        if (a->kind == VK_CONST) { x = vi_const(a->abits[i], t->elem.w, t->elem.ty); x.tid = t->elem.ut; return x; }
        x = vi_mk(vi_syms[a->elem0 + i].wname, t->elem.ty, t->elem.w, 2);
        x.tid = t->elem.ut;
        return x;
    }
    if (v->rule == VHDL_R_aggregate) {
        gram_node *el, *found = NULL, *others = NULL;
        int pos = 0;
        for (el = v->child; el; el = el->next) {
            gram_node *ch = el->child->rule == VHDL_R_choices ? el->child : NULL, *e = ch ? ch->next : el->child, *c;
            if (!ch) { if (pos++ == i) found = e; continue; }
            for (c = ch->child; c; c = c->next) {
                long k;
                if (c->rule == VHDL_R_OTHERS) others = e;
                else if (!vi_ceval(c, &k)) vi_fail(c, "a choice in an array aggregate must be a constant index");
                else if (k == idx) found = e;
            }
        }
        if (!found) found = others;
        if (!found) vi_fail(v, "element %ld of this aggregate has no value; add others => ...", idx);
        x = vi_gen(found, t->elem.w);
        vi_assignable(&tt, &x, found);
        x.tid = t->elem.ut;
        return x;
    }
    vi_fail(at, "an array is given a value from another array or an aggregate, like (others => (others => '0'))");
    memset(&x, 0, sizeof x);
    return x;
}

/* the bits a slice or index of s picks: 1 for one bit */
static int vi_bitrange(VSym *s, gram_node *id, gram_node *arg, int *hi, int *lo)
{
    VSym tmp = *s;
    VX x;
    tmp.kind = VK_SIG;
    tmp.wname = "@";
    x = vi_index(&tmp, id, arg);   /* "@[3:0]" or "@[2]" or "@" */
    if (!strcmp(x.s, "@")) { *hi = s->w - 1; *lo = 0; return 0; }
    if (sscanf(x.s, "@[%d:%d]", hi, lo) == 2) return 0;
    sscanf(x.s, "@[%d]", hi);
    *lo = *hi;
    return 1;
}

static void vi_seq_assign(gram_node *target, gram_node *val, int isvar)
{
    gram_node *id = target->child, *a1 = id->next, *a2 = a1 ? a1->next : NULL;
    gram_node *v = val->rule == VHDL_R_wave ? val->child : val;
    VSym *s = vi_find(id);
    VTarget tt;
    VX x;
    int si;
    memset(&tt, 0, sizeof tt);
    if (!s) vi_fail(id, "there is no signal or variable called %.*s", VT(id));
    if (s->kind == VK_IN) vi_fail(id, "%.*s is an input of the entity, so it cannot be given a value here", VT(id));
    if (s->kind == VK_CONST || s->kind == VK_GEN) vi_fail(id, "%.*s is a constant", VT(id));
    if (s->kind == VK_LABEL) vi_fail(id, "%.*s is the label of an instance, not a signal", VT(id));
    if (isvar && s->kind != VK_VAR) vi_fail(id, "%.*s is a signal; give it a value with <=", VT(id));
    if (!isvar && s->kind == VK_VAR) vi_fail(id, "%.*s is a variable; give it a value with :=", VT(id));
    si = (int)(s - vi_syms);
    if (s->ty == T_ARR) {
        VType *t = &vi_types[s->ut - 1];
        int i, n = t->ahi - t->alo + 1, e0 = s->elem0;
        long k;
        if (!a1) {   /* the whole array */
            for (i = 0; i < n; i++) vi_put(vi_env, e0 + i, vi_array_elem(t, v, i, v), -1, -1);
            return;
        }
        if (!vi_ceval(a1, &k)) {   /* an element picked by a signal: each element, when the index is its own */
            VX ix = vi_gen(a1, -1);
            if (a2) vi_fail(a2, "a slice of an element picked by a signal is not supported yet");
            if (ix.ty != T_INTV && ix.ty != T_UNS) vi_fail(a1, "an index is an integer: write %.*s(to_integer(...))", VT(id));
            tt.s = "an element"; tt.ty = t->elem.ty; tt.w = t->elem.w; tt.ut = t->elem.ut;
            x = vi_gen(v, t->elem.w);
            vi_assignable(&tt, &x, v);
            for (i = 0; i < n; i++) {
                long idx = t->alo + i;
                if (ix.w < 31 && idx >= (1L << ix.w)) continue;
                vi_put_when(vi_env, e0 + i, x, vi_eq_code(ix, (unsigned long)idx));
            }
            return;
        }
        if (k < t->alo || k > t->ahi) vi_fail(a1, "%ld is outside %.*s(%d to %d)", k, VT(id), t->alo, t->ahi);
        si = e0 + (int)(k - t->alo);
        s = &vi_syms[si];
        a1 = a2; a2 = NULL;
    }
    if (!a1) {
        tt.s = s->wname; tt.ty = s->ty; tt.w = s->w; tt.sym = s; tt.ut = s->ut;
        x = vi_gen(v, s->w);
        vi_assignable(&tt, &x, v);
        vi_put(vi_env, si, x, -1, -1);
        return;
    }
    if (a2) vi_fail(a2, "one index or slice at a time, please");
    if (a1->rule != VHDL_R_drange && vi_vec(s->ty) && !vi_ceval(a1, &(long){0})) {
        /* a bit picked by a signal: each bit, when the index is its own */
        VX ix = vi_gen(a1, -1);
        int j, lo = vi_low(s), hi = vi_high(s);
        if (ix.ty != T_INTV && ix.ty != T_UNS) vi_fail(a1, "an index is an integer: write %.*s(to_integer(...))", VT(id));
        tt.s = s->wname; tt.ty = T_SL; tt.w = 1; tt.sym = s;
        x = vi_gen(v, 1);
        vi_assignable(&tt, &x, v);
        {
            VSlot *sl = vi_slot(vi_env, si);
            VX old = sl->v, nv;
            VBuf b = { 0, 0, 0 };
            int k;
            vi_bind(&old); vi_bind(&x); vi_bind(&ix);
            vb_add(&b, "{", 1);
            for (k = s->w - 1; k >= 0; k--) {
                VX ob = old.cst ? vi_const((old.v >> k) & 1, 1, T_SL) : vi_mk(vi_sl(vi_wrap(&old, 2), k, k), T_SL, 1, 2), nb = ob;
                j = s->down ? k + lo : hi - k;
                if (ix.w >= 31 || j < (1L << ix.w)) {
                    nb = vi_mux2(ob, x, vi_eq_code(ix, (unsigned long)j));
                    if (!vi_live(vi_env)) nb = vi_mux2(ob, nb, vi_env->live);
                }
                vbf(&b, "%s%s", k == s->w - 1 ? "" : ", ", nb.s);
            }
            vb_add(&b, "}", 1);
            nv = vi_mk(b.s, s->ty, s->w, 2);
            nv.cat = 1;
            nv.tid = s->ut;
            sl->v = nv;
        }
        return;
    }
    {
        int hi, lo, one = vi_bitrange(s, id, a1, &hi, &lo);
        tt.s = s->wname; tt.ty = one ? T_SL : s->ty; tt.w = hi - lo + 1; tt.sym = s;
        x = vi_gen(v, hi - lo + 1);
        vi_assignable(&tt, &x, v);
        vi_put(vi_env, si, x, hi, lo);
    }
}

/* if c then body [elsif ...] [else ...] end if: both ways, then a mux per value */
static void vi_seq_if(gram_node *cond, gram_node *body, gram_node *rest)
{
    VEnv *pre = vi_env, t, f;
    VX c = vi_cond(cond);
    if (c.cst) {   /* a constant condition (a generic, say): only one way is taken */
        if (c.v) vi_seq(body->child);
        else if (rest && rest->rule == VHDL_R_elsifp) vi_seq_if(rest->child, rest->child->next, rest->next);
        else if (rest) vi_seq(rest->child->child);
        return;
    }
    t = vi_env_copy(pre);
    f = vi_env_copy(pre);
    vi_env = &t;
    vi_seq(body->child);
    vi_env = &f;
    if (rest && rest->rule == VHDL_R_elsifp) vi_seq_if(rest->child, rest->child->next, rest->next);
    else if (rest) vi_seq(rest->child->child);
    vi_env = pre;
    vi_merge(pre, &t, &f, c);
}

/* case s is when ... => ...: every branch, then one mux per value with sel = s
 * (or, for a wide selector, a chain of comparisons) */
static void vi_seq_case(gram_node *st)
{
    gram_node *k = st->child, *seln, *w, *ch;
    VEnv *pre = vi_env, *envs;
    VX sel;
    int nb = 0, b, others = -1, ncodes = 0, cap = 0, i, j, total;
    unsigned long *codes = NULL;
    int *owner_of = NULL;
    if (k->rule == VHDL_R_label) k = k->next;
    seln = k;
    sel = vi_gen(seln, -1);
    if (sel.ty == T_INT || sel.ty == T_ARR) vi_fail(seln, "case selects on a signal: a vector, an integer or a state");
    for (w = seln->next; w && w->rule == VHDL_R_casew; w = w->next) nb++;
    envs = (VEnv *)calloc((size_t)nb, sizeof *envs);
    for (b = 0, w = seln->next; b < nb; b++, w = w->next) {
        for (ch = w->child->child; ch; ch = ch->next) {
            VX cv;
            if (ch->rule == VHDL_R_OTHERS) {
                if (b != nb - 1 || ch != w->child->child || ch->next) vi_fail(ch, "when others goes alone, in the last when");
                others = b;
                continue;
            }
            cv = vi_gen(ch, sel.w);
            if (cv.ty == T_INT) cv = vi_int_as(&cv, sel.w, sel.ty == T_INTV ? T_INTV : T_UNS, ch);
            if (!cv.cst) vi_fail(ch, "each choice must be a constant");
            if (sel.ty == T_ENUM && (cv.ty != T_ENUM || cv.tid != sel.tid)) vi_fail(ch, "this is not one of the states of the selector");
            if (cv.w != sel.w) vi_fail(ch, "the choice is %d bit%s, but the selector is %d", cv.w, cv.w == 1 ? "" : "s", sel.w);
            for (i = 0; i < ncodes; i++) if (codes[i] == cv.v) vi_fail(ch, "this value is chosen twice");
            if (ncodes == cap) {
                cap = cap ? cap * 2 : 16;
                codes = (unsigned long *)realloc(codes, (size_t)cap * sizeof *codes);
                owner_of = (int *)realloc(owner_of, (size_t)cap * sizeof *owner_of);
            }
            codes[ncodes] = cv.v;
            owner_of[ncodes++] = b;
        }
        envs[b] = vi_env_copy(pre);
        vi_env = &envs[b];
        vi_seq(w->child->next->child);
        vi_env = pre;
    }
    total = sel.ty == T_ENUM ? vi_types[sel.tid - 1].n : sel.w < 17 ? 1 << sel.w : -1;
    if (others < 0) {
        if (total < 0) vi_fail(st, "a case on %d bits must have when others", sel.w);
        for (j = 0; j < total; j++) {
            for (i = 0; i < ncodes && codes[i] != (unsigned long)j; i++) {}
            if (i == ncodes) {
                if (sel.ty == T_ENUM) {
                    gram_node *l = vi_types[sel.tid - 1].lits;
                    int q;
                    for (q = 0; q < j; q++) l = l->next;
                    vi_fail(st, "this case does not choose %.*s; add it, or when others => null;", VT(l));
                }
                vi_fail(st, "this case does not cover %s; add when others => ...", vi_lit((unsigned long)j, sel.w));
            }
        }
    }
    if (sel.w <= 4) {   /* a table: one mux per value, with a data input per code */
        int n = 1 << sel.w, *own = (int *)malloc((size_t)n * sizeof *own), pass;
        VEnv m;
        memset(&m, 0, sizeof m);
        for (j = 0; j < n; j++) own[j] = others >= 0 ? others : nb - 1;   /* unused codes: whichever */
        for (i = 0; i < ncodes; i++) own[codes[i]] = owner_of[i];
        for (pass = 0; pass <= nb; pass++) {
            int lim = pass < nb ? envs[pass].n : 1;
            for (i = 0; i < lim; i++) {
                int si = pass < nb ? envs[pass].s[i].sym : -1;
                VX *vals = (VX *)malloc((size_t)n * sizeof *vals);
                unsigned long mask = ~0UL;
                int same = 1;
                VBuf bb = { 0, 0, 0 };
                if (pass == nb) {   /* live, the last time round */
                    for (j = 0; j < n; j++) vals[j] = envs[own[j]].live;
                } else {
                    if (vi_slot_find(&m, si)) { free(vals); continue; }
                    for (j = 0; j < n; j++) {
                        VSlot *sl = vi_slot_find(&envs[own[j]], si);
                        unsigned long mk;
                        if (sl) { vals[j] = sl->v; mk = sl->mask; } else vals[j] = vi_slot_init(si, &mk);
                        mask &= mk;
                    }
                }
                for (j = 1; j < n; j++) if (strcmp(vals[j].s, vals[0].s)) same = 0;
                if (!same) {
                    VX r;
                    vb_add(&bb, "mux(", 4);
                    for (j = 0; j < n; j++) vbf(&bb, "%s, ", vals[j].s);
                    vbf(&bb, "sel = %s)", sel.s);
                    r = vi_mk(bb.s, vals[0].ty, vals[0].w, 2);
                    r.tid = vals[0].tid;
                    vals[0] = r;
                }
                if (pass == nb) m.live = vals[0];
                else { VSlot *o = vi_slot(&m, si); o->v = vals[0]; o->mask = mask; }
                free(vals);
            }
        }
        *pre = m;
        free(own);
        return;
    }
    {   /* a wide selector: the default, then each branch in turn */
        int def = others >= 0 ? others : nb - 1;
        VEnv acc = envs[def];
        for (b = nb - 1; b >= 0; b--) {
            VBuf cb = { 0, 0, 0 };
            int m = 0;
            VX c;
            if (b == def) continue;
            for (i = 0; i < ncodes; i++)
                if (owner_of[i] == b) vbf(&cb, "%s%s", m++ ? " | " : "", vi_wrap(&(VX){ .s = vi_eq_code(sel, codes[i]).s, .atom = 2 }, 1));
            c = vi_mk(cb.s, T_BOOL, 1, m > 1 ? 0 : 2);
            vi_merge(&acc, &envs[b], &acc, c);
        }
        *pre = acc;
    }
}

static int vi_loops;   /* how deep in for loops: exit is allowed inside one */

/* for i in a to b loop ... end loop: once per value (exit stops the rest of the loop) */
static void vi_seq_loop(gram_node *st)
{
    gram_node *k = st->child, *var, *r, *body;
    long from, to, step, i, n = 0;
    VX live = vi_env->live;
    if (k->rule == VHDL_R_label) k = k->next;
    var = k; r = var->next; body = r->next;
    from = vi_const_int(r->child, "the range of a loop");
    to = vi_const_int(r->child->next->next, "the range of a loop");
    step = vi_is(r->child->next, "downto") ? -1 : 1;
    if ((to - from) * step < 0) return;
    vi_loops++;
    for (i = from; ; i += step) {
        int mark = vi_nsyms;
        VSym *v;
        if (++n > 1024) vi_fail(r, "this loop runs more than 1024 times");
        v = vi_declare(var, VK_CONST);
        v->ty = T_INT;
        v->ival = i;
        vi_seq(body->child);
        vi_nsyms = mark;
        if (i == to) break;
    }
    vi_loops--;
    vi_env->live = live;   /* after the loop, everything runs again */
}

static void vi_seq_exit(gram_node *st)
{
    const char *p = st->child->text + st->child->len;
    gram_node *c = NULL;
    if (!vi_loops) vi_fail(st, "exit belongs inside a for loop");
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (st->child->next && ((p[0] | 32) == 'w' && (p[1] | 32) == 'h' && (p[2] | 32) == 'e' && (p[3] | 32) == 'n'))
        c = st->child->next;
    else if (st->child->next && st->child->next->next) c = st->child->next->next;
    if (!c) { vi_env->live = vi_const(0, 1, T_BOOL); return; }
    {
        VX cond = vi_cond(c), stay = vi_not(cond);
        stay.ty = T_BOOL;
        if (vi_live(vi_env)) vi_env->live = stay;
        else vi_env->live = vi_mk(vi_fmt("%s & %s", vi_wrap(&vi_env->live, 1), vi_wrap(&stay, 1)), T_BOOL, 1, 0);
    }
}

static int vi_ret_si = -1;       /* inside a function: the VSym its return value goes to */

static void vi_seq_return(gram_node *st)
{
    VSym *r;
    VX x;
    VTarget tt;
    if (vi_ret_si < 0) vi_fail(st, "return belongs inside a function");
    if (vi_loops) vi_fail(st, "return inside a loop is not supported yet; set a variable and exit");
    r = &vi_syms[vi_ret_si];
    x = vi_gen(st->child, r->w ? r->w : -1);
    if (!r->w) {   /* std_logic_vector, unsigned ...: as wide as what it returns */
        if (x.ty == T_INT || x.w < 1) vi_fail(st->child, "Wire cannot tell how wide this is; give the function's return type a range");
        r->w = x.w; r->left = x.w - 1; r->right = 0; r->down = 1;
    }
    memset(&tt, 0, sizeof tt);
    tt.s = "the return value"; tt.ty = r->ty; tt.w = r->w; tt.ut = r->ut;
    vi_assignable(&tt, &x, st->child);
    vi_put(vi_env, vi_ret_si, x, -1, -1);
    vi_slot(vi_env, vi_ret_si)->mask = vi_mask(r->w);   /* the paths not live here returned earlier */
    vi_env->live = vi_const(0, 1, T_BOOL);   /* nothing after it runs */
}

static void vi_seq(gram_node *first)
{
    gram_node *st;
    for (st = first; st; st = st->next) {
        switch (st->rule) {
        case VHDL_R_sassign: vi_seq_assign(st->child, st->child->next, 0); break;
        case VHDL_R_vassign: vi_seq_assign(st->child, st->child->next, 1); break;
        case VHDL_R_ifst: {
            gram_node *k = st->child;
            if (k->rule == VHDL_R_label) k = k->next;
            vi_seq_if(k, k->next, k->next->next && k->next->next->rule != VHDL_R_ident ? k->next->next : NULL);
            break;
        }
        case VHDL_R_casest: vi_seq_case(st); break;
        case VHDL_R_loopst: vi_seq_loop(st); break;
        case VHDL_R_exitst: vi_seq_exit(st); break;
        case VHDL_R_retst: vi_seq_return(st); break;
        case VHDL_R_nullst: case VHDL_R_assert: break;
        case VHDL_R_ident: break;   /* end function name; */
        case VHDL_R_waitst: vi_fail(st, "wait is for simulation; a process for hardware has a sensitivity list instead"); break;
        default: vi_fail(st, "this kind of statement is not supported yet in a process");
        }
    }
}

/* ---- functions: inlined where they are called --------------------------- */

static int vi_fparam_count(gram_node *fp)
{
    int n = 0;
    gram_node *p, *id;
    for (p = fp ? fp->child : NULL; p; p = p->next)
        for (id = p->child->child; id; id = id->next) n++;
    return n;
}

static gram_node *vi_find_func(const gram_node *id, int nargs)
{
    int i;
    for (i = vi_nfuncs - 1; i >= 0; i--) {
        gram_node *f = vi_funcs[i], *name = f->child;
        if (name->len != id->len || !vi_is(id, vi_fmt("%.*s", VT(name)))) continue;
        if (vi_fparam_count(name->next->rule == VHDL_R_fparams ? name->next : NULL) == nargs) return f;
    }
    return NULL;
}

static int vi_unconstrained(gram_node *t, VSym *s)   /* std_logic_vector without a range: its width comes later */
{
    gram_node *id = t->child;
    if (id->next) return 0;
    if (vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector") || vi_is(id, "bit_vector")) s->ty = T_SLV;
    else if (vi_is(id, "unsigned")) s->ty = T_UNS;
    else if (vi_is(id, "signed")) s->ty = T_SGN;
    else return 0;
    s->w = 0;
    return 1;
}

static int vi_fdepth;

static VX vi_func_call(gram_node *f, gram_node *id, gram_node *args)
{
    gram_node *name = f->child, *k = name->next, *fp = NULL, *rty, *pd, *body, *p, *pid;
    VSym *ps = NULL;
    VX *pv = NULL, res;
    int np = 0, i, mark = vi_nsyms, tmark = vi_ntypes, fmark = vi_nfuncs, scope = vi_scope;
    int clocked = vi_clocked, loops = vi_loops, ret = vi_ret_si, rsi;
    const char *bind_at = vi_bind_at;
    VEnv env, *outer = vi_env;
    VSlot *sl;
    if (k->rule == VHDL_R_fparams) { fp = k; k = k->next; }
    rty = k; pd = rty->next; body = pd->next;
    if (vi_fdepth >= 16) vi_fail(id, "functions call each other too deeply (a function calling itself is not supported)");
    /* the arguments, worked out where the function is called */
    ps = (VSym *)calloc(64, sizeof *ps);
    pv = (VX *)calloc(64, sizeof *pv);
    for (p = fp ? fp->child : NULL; p; p = p->next) {
        gram_node *t = vi_kid(p, VHDL_R_type);
        VSym proto;
        int open;
        memset(&proto, 0, sizeof proto);
        open = vi_unconstrained(t, &proto);
        if (!open) vi_type(t, &proto, 1);
        if (proto.ty == T_ARR) vi_fail(t, "an array argument is not supported yet");
        for (pid = p->child->child; pid; pid = pid->next) {
            gram_node *a = vi_arg(args, np);
            VX x;
            if (np >= 64) vi_fail(args, "more than 64 arguments");
            if (a->rule == VHDL_R_named) vi_fail(a, "give a function's arguments in order, without names");
            ps[np] = proto;
            ps[np].name = pid->text; ps[np].nlen = (int)pid->len; ps[np].at = pid;
            x = vi_gen(a, proto.ty == T_INT ? -1 : open ? -1 : proto.w);
            if (proto.ty == T_INT) {
                if (!x.cst || x.ty != T_INT) vi_fail(a, "an integer argument must be a constant here");
                ps[np].ival = x.iv;
            } else if (open) {
                VSym *as = a->rule == VHDL_R_ident ? vi_find(a) : NULL;
                if (x.ty == T_INT || x.w < 1 || x.ty == T_SL) vi_fail(a, "this argument is a vector");
                ps[np].w = x.w; ps[np].left = x.w - 1; ps[np].right = 0; ps[np].down = 1;
                if (as && as->w == x.w && vi_vec(as->ty)) { ps[np].left = as->left; ps[np].right = as->right; ps[np].down = as->down; }
                if (x.ty == T_STR) x.ty = proto.ty;
            } else {
                VTarget tt;
                memset(&tt, 0, sizeof tt);
                tt.s = vi_fmt("%.*s", VT(pid)); tt.ty = proto.ty; tt.w = proto.w; tt.ut = proto.ut;
                vi_assignable(&tt, &x, a);
            }
            pv[np++] = x;
        }
    }
    /* the body, in a scope and a set of values of its own */
    memset(&env, 0, sizeof env);
    env.live = vi_const(1, 1, T_BOOL);
    vi_scope = vi_nsyms;
    vi_env = &env; vi_clocked = 0; vi_loops = 0;
    for (i = 0; i < np; i++) {
        VSym *s = vi_declare((gram_node *)ps[i].at, ps[i].ty == T_INT ? VK_CONST : VK_VAR);
        s->ty = ps[i].ty; s->w = ps[i].w; s->left = ps[i].left; s->right = ps[i].right; s->down = ps[i].down;
        s->ut = ps[i].ut; s->ival = ps[i].ival;
        if (s->kind == VK_VAR) vi_put(&env, (int)(s - vi_syms), pv[i], -1, -1);
    }
    {
        VSym proto, *r;
        memset(&proto, 0, sizeof proto);
        if (!vi_unconstrained(rty, &proto)) vi_type(rty, &proto, 0);
        if (proto.ty == T_ARR) vi_fail(rty, "a function returning an array is not supported yet");
        r = vi_declare(name, VK_VAR);
        r->ty = proto.ty; r->w = proto.w; r->left = proto.left; r->right = proto.right; r->down = proto.down; r->ut = proto.ut;
        r->nlen = 0;   /* not found by name: a call of the function is not its value */
        rsi = (int)(r - vi_syms);
    }
    vi_ret_si = rsi;
    if (pd->child) vi_decls(pd->child);
    for (i = rsi + 1; i < vi_nsyms; i++)   /* variables that start with a value */
        if (vi_syms[i].kind == VK_VAR && vi_syms[i].hasinit) vi_put(&env, i, vi_const(vi_syms[i].bits, vi_syms[i].w, vi_syms[i].ty), -1, -1);
    if (!bind_at) vi_bind_at = id->text;
    vi_fdepth++;
    vi_seq(body && body->rule != VHDL_R_ident ? body : NULL);
    vi_fdepth--;
    vi_bind_at = bind_at;
    sl = vi_slot_find(&env, rsi);
    if (!sl || !vi_syms[rsi].w || sl->mask != vi_mask(vi_syms[rsi].w))
        vi_fail(id, "%.*s does not return a value on every path", VT(id));
    res = sl->v;
    res.ty = vi_syms[rsi].ty;
    res.tid = vi_syms[rsi].ut;
    vi_env = outer; vi_clocked = clocked; vi_loops = loops; vi_ret_si = ret;
    vi_scope = scope;
    vi_nsyms = mark; vi_ntypes = tmark; vi_nfuncs = fmark;
    free(ps); free(pv);
    return res;
}

/* rising_edge(clk), falling_edge(clk), clk'event and clk = '1' */
static int vi_edge(gram_node *c, gram_node **clk, int *neg)
{
    if (c->rule == VHDL_R_name && (vi_is(c->child, "rising_edge") || vi_is(c->child, "falling_edge"))) {
        gram_node *args = c->child->next;
        if (!args || args->rule != VHDL_R_args || vi_nargs(args) != 1 || args->child->rule != VHDL_R_ident || args->next)
            vi_fail(c, "%.*s takes the clock signal", VT(c->child));
        *clk = args->child;
        *neg = vi_is(c->child, "falling_edge");
        return 1;
    }
    if (c->rule == VHDL_R_expr && c->nchildren == 3 && vi_is(c->child->next, "and")) {
        gram_node *a = c->child, *b = c->child->next->next, *ev, *lv;
        int i;
        for (i = 0; i < 2; i++) {
            ev = i ? b : a; lv = i ? a : b;
            if (ev->rule == VHDL_R_name && ev->child->next && ev->child->next->rule == VHDL_R_attr
                && vi_is(ev->child->next->child, "event") && lv->rule == VHDL_R_relation
                && lv->child->rule == VHDL_R_ident && vi_is(lv->child->next, "=") && lv->child->next->next->rule == VHDL_R_chr
                && lv->child->len == ev->child->len && vi_is(lv->child, vi_fmt("%.*s", VT(ev->child)))) {
                *clk = ev->child;
                *neg = lv->child->next->next->text[1] == '0';
                return 1;
            }
        }
    }
    return 0;
}

static int vi_has_edge(const gram_node *n)   /* a clock edge anywhere in n */
{
    const gram_node *k;
    if (n->rule == VHDL_R_name && (vi_is(n->child, "rising_edge") || vi_is(n->child, "falling_edge"))) return 1;
    if (n->rule == VHDL_R_attr && (vi_is(n->child, "event") || vi_is(n->child, "stable"))) return 1;
    for (k = n->child; k; k = k->next) if (vi_has_edge(k)) return 1;
    return 0;
}

/* a process's parts: its declarations and its first statement */
static gram_node *vi_proc_body(gram_node *st, gram_node **decls)
{
    gram_node *k;
    *decls = NULL;
    for (k = st->child; k; k = k->next) {
        if (k->rule == VHDL_R_pdecls) { *decls = k; return k->next; }
    }
    return NULL;
}

/* the clocked shape: if rising_edge(clk) then ... end if, or
 * if rst = '1' then <constants> elsif rising_edge(clk) then ... end if */
static int vi_proc_shape(gram_node *first, gram_node **clk, int *neg, gram_node **rst, gram_node **rbody, gram_node **cbody)
{
    gram_node *k = first, *c, *body, *rest, *st = NULL;
    int n = 0;
    for (k = first; k; k = k->next) if (k->rule != VHDL_R_ident && k->rule != VHDL_R_nullst && k->rule != VHDL_R_assert) { st = k; n++; }
    *rst = *rbody = *cbody = NULL;
    if (n != 1 || st->rule != VHDL_R_ifst) return 0;
    c = st->child;
    if (c->rule == VHDL_R_label) c = c->next;
    body = c->next;
    rest = body->next && body->next->rule != VHDL_R_ident ? body->next : NULL;
    if (vi_edge(c, clk, neg)) {
        if (rest) vi_fail(rest, "a clocked process has nothing after its if rising_edge(clk) part; put the logic inside it");
        *cbody = body;
        return 1;
    }
    if (rest && rest->rule == VHDL_R_elsifp && vi_edge(rest->child, clk, neg)) {
        if (rest->next && rest->next->rule != VHDL_R_ident)
            vi_fail(rest->next, "a clocked process has nothing after its elsif rising_edge(clk) part");
        *rst = c; *rbody = body; *cbody = rest->child->next;
        return 1;
    }
    return 0;
}

static void vi_emit_reg(gram_node *at, VSym *s, VX next, const char *clk, const char *rst, int has_reset, unsigned long k)
{
    const char *q = s->wname, *en = NULL;
    VX d = next;
    char *call, *ds;
    if (next.m0 && !strcmp(next.m0, q)) { en = next.ms; d = vi_mk(next.m1, s->ty, s->w, 0); }   /* if en: an enable */
    if (rst && !has_reset)   /* not reset: keeps its value while the reset is on */
        en = en ? vi_fmt("(%s) & ~(%s)", en, rst) : vi_fmt("~(%s)", rst);
    k &= vi_mask(s->w);
    ds = !k ? d.s : k == vi_mask(s->w) ? vi_fmt("~(%s)", d.s) : vi_fmt("(%s) ^ %s", d.s, vi_lit(k, s->w));
    call = vi_fmt("dff(%s, %s%s%s%s%s)", ds, clk, en ? ", en = " : "", en ? en : "", rst && has_reset ? ", rst = " : "",
                  rst && has_reset ? rst : "");
    /* a register that resets (or starts) at k, not 0: it keeps the value ^ k */
    if (k) call = k == vi_mask(s->w) ? vi_fmt("~%s", call) : vi_fmt("%s ^ %s", call, vi_lit(k, s->w));
    vi_emit(at, "%s = %s", q, call);
}

/* the reset part: each signal gets a constant */
typedef struct { int sym; unsigned long v; } VReset;

static int vi_reset_vals(gram_node *first, VReset **out)
{
    gram_node *st;
    int n = 0, cap = 0;
    VReset *r = NULL;
    for (st = first; st; st = st->next) {
        gram_node *id, *a1, *v;
        VSym *s;
        int i, cnt = 1, base;
        if (st->rule == VHDL_R_nullst || st->rule == VHDL_R_assert) continue;
        if (st->rule != VHDL_R_sassign)
            vi_fail(st, "the reset part of a clocked process only gives signals constant values (<=)");
        id = st->child->child; a1 = id->next;
        v = st->child->next->rule == VHDL_R_wave ? st->child->next->child : st->child->next;
        s = vi_find(id);
        if (!s || s->kind == VK_IN || s->kind == VK_CONST || s->kind == VK_GEN || s->kind == VK_LABEL || s->kind == VK_VAR)
            vi_fail(id, "%.*s cannot be given a value here", VT(id));
        base = (int)(s - vi_syms);
        if (s->ty == T_ARR) {
            VType *t = &vi_types[s->ut - 1];
            long k;
            if (a1) {
                if (!vi_ceval(a1, &k) || a1->next) vi_fail(a1, "reset an element with a constant index");
                base = s->elem0 + (int)(k - t->alo);
            } else { base = s->elem0; cnt = t->ahi - t->alo + 1; }
            for (i = 0; i < cnt; i++) {
                VX x = a1 ? vi_gen(v, t->elem.w) : vi_array_elem(t, v, i, v);
                if (!x.cst) vi_fail(v, "a reset value must be a constant");
                if (n == cap) { cap = cap ? cap * 2 : 16; r = (VReset *)realloc(r, (size_t)cap * sizeof *r); }
                r[n].sym = base + i; r[n++].v = x.v;
            }
            continue;
        }
        if (a1) vi_fail(a1, "reset the whole signal, not a part of it");
        {
            VTarget tt;
            VX x = vi_gen(v, s->w);
            memset(&tt, 0, sizeof tt);
            tt.s = s->wname; tt.ty = s->ty; tt.w = s->w; tt.ut = s->ut;
            vi_assignable(&tt, &x, v);
            if (!x.cst) vi_fail(v, "a reset value must be a constant");
            if (n == cap) { cap = cap ? cap * 2 : 16; r = (VReset *)realloc(r, (size_t)cap * sizeof *r); }
            r[n].sym = base; r[n++].v = x.v;
        }
    }
    *out = r;
    return n;
}

static void vi_process(gram_node *st)
{
    gram_node *decls, *first = vi_proc_body(st, &decls), *clk = NULL, *rst = NULL, *rbody = NULL, *cbody = NULL;
    gram_node *at = vi_kid(st, VHDL_R_PROCESS) ? vi_kid(st, VHDL_R_PROCESS) : st;
    int mark = vi_nsyms, tmark = vi_ntypes, fmark = vi_nfuncs, neg = 0, clocked, i;
    VEnv env;
    memset(&env, 0, sizeof env);
    env.live = vi_const(1, 1, T_BOOL);
    if (decls) vi_decls(decls->child);
    clocked = vi_proc_shape(first, &clk, &neg, &rst, &rbody, &cbody);
    if (!clocked) {
        gram_node *k;
        for (k = first; k; k = k->next)
            if (vi_has_edge(k))
                vi_fail(k, "Wire reads a clocked process written as  if rising_edge(clk) then ... end if;  or as  "
                           "if rst = '1' then ... elsif rising_edge(clk) then ... end if;  with nothing else around it");
    }
    vi_clocked = clocked;
    vi_env = &env;
    vi_bind_at = at->text;
    vi_seq(clocked ? cbody->child : first);
    vi_bind_at = NULL;
    vi_env = NULL;
    vi_clocked = 0;
    if (!clocked) {   /* logic: every signal must get a value on every path */
        for (i = 0; i < env.n; i++) {
            VSym *s = &vi_syms[env.s[i].sym];
            if (s->kind == VK_VAR) continue;
            if (env.s[i].mask != vi_mask(s->w))
                vi_fail(at, "%s is not given a value on every path through this process, so it would need memory (a latch); "
                            "give it a value at the top of the process", s->wname);
            vi_emit(at, "%s = %s", s->wname, env.s[i].v.s);
        }
    } else {   /* registers */
        VSym *cs = vi_find(clk);
        VReset *rv = NULL;
        int nr = 0, pass;
        char *clks, *rsts = NULL;
        if (!cs || cs->w != 1 || cs->ty == T_ARR || cs->kind == VK_VAR) vi_fail(clk, "the clock must be a one-bit signal or port");
        clks = neg ? vi_fmt("~%s", cs->wname) : cs->wname;
        if (rst) {
            VX r = vi_cond(rst);
            rsts = r.s;
            nr = vi_reset_vals(rbody->child, &rv);
        }
        for (pass = 0; pass < 2; pass++) {
            int lim = pass ? nr : env.n;
            for (i = 0; i < lim; i++) {
                int si = pass ? rv[i].sym : env.s[i].sym, j, has = 0;
                unsigned long k = 0;
                VSym *s = &vi_syms[si];
                VSlot *sl;
                VX next;
                if (s->kind == VK_VAR) continue;
                if (pass && vi_slot_find(&env, si)) continue;   /* done in the first pass */
                for (j = 0; j < nr; j++) if (rv[j].sym == si) { has = 1; k = rv[j].v; }
                if (!has && s->hasinit) k = s->bits;
                sl = vi_slot_find(&env, si);
                if (sl) next = sl->v;
                else { next = vi_mk(s->wname, s->ty, s->w, 2); }
                vi_emit_reg(at, s, next, clks, rsts, has, k);
            }
        }
    }
    vi_nsyms = mark;
    vi_ntypes = tmark;
    vi_nfuncs = fmark;
}

/* the clocks of the processes, before the ports are written: they become clock, not input */

static void vi_scan_clocks_in(gram_node *first, gram_node **out, int *n, int depth);

/* the clocks used in the entity e: a port given to a chip's clock is a clock too */
static int vi_ent_clocks(VEnt *e, gram_node **out, int depth)
{
    int n = 0;
    if (depth < 16) vi_scan_clocks_in(e->arch->child->next->next, out, &n, depth + 1);
    return n;
}

static void vi_scan_clocks_in(gram_node *first, gram_node **out, int *n, int depth)
{
    gram_node *k;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_process) {
            gram_node *decls, *b = vi_proc_body(k, &decls), *c = NULL, *r, *rb, *cb;
            int neg;
            if (vi_proc_shape(b, &c, &neg, &r, &rb, &cb)) {
                if (*n < 64) out[(*n)++] = c;
                if (!depth) vi_scan_regs(cb->child);
            }
        } else if (k->rule == VHDL_R_inst) {
            VEnt *e = vi_ent_named(vi_inst_name(k));
            gram_node *inner[64], *pm = vi_kid(k, VHDL_R_pmap), *as;
            int m, i, pos = 0;
            if (!e || !pm) continue;
            m = vi_ent_clocks(e, inner, depth);
            for (as = pm->child; as; as = as->next, pos++) {
                gram_node *f = as->child->next ? as->child : NULL, *v = f ? f->next : as->child, *formal = NULL;
                if (v->rule != VHDL_R_ident) continue;
                if (f) formal = f->child;
                else {   /* by position: the pos-th port of the entity */
                    gram_node *pd = vi_kid(e->ent, VHDL_R_ports), *d, *id;
                    int q = 0;
                    for (d = pd ? pd->child : NULL; d && !formal; d = d->next)
                        for (id = d->child->child; id; id = id->next, q++) if (q == pos) { formal = id; break; }
                }
                if (!formal) continue;
                for (i = 0; i < m; i++)
                    if (inner[i]->len == formal->len && vi_is(formal, vi_fmt("%.*s", VT(inner[i])))) {
                        if (*n < 64) out[(*n)++] = v;
                        break;
                    }
            }
        } else if (k->rule == VHDL_R_generate) {
            gram_node *g = k->child->next, *b = g->child->next;
            if (g->rule == VHDL_R_forgen) b = b->next;
            if (b && b->rule == VHDL_R_gdecls) b = b->next;
            vi_scan_clocks_in(b, out, n, depth);
        }
    }
}

static void vi_scan_clocks(gram_node *first)
{
    vi_nclk = 0;
    vi_nregs = 0;
    vi_scan_clocks_in(first, vi_clk, &vi_nclk, 0);
}

/* the signals clocked processes give values to (outputs among them need their width written) */

static void vi_scan_regs(gram_node *first)
{
    gram_node *k;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_sassign && vi_nregs < 256) vi_regs[vi_nregs++] = k->child->child;
        else if (k->rule == VHDL_R_ifst || k->rule == VHDL_R_elsifp || k->rule == VHDL_R_elsep || k->rule == VHDL_R_sbody
                 || k->rule == VHDL_R_casest || k->rule == VHDL_R_casew || k->rule == VHDL_R_loopst)
            vi_scan_regs(k->child);
    }
}

static int vi_is_reg(const gram_node *id)
{
    int i;
    for (i = 0; i < vi_nregs; i++) if (vi_regs[i]->len == id->len && vi_is(id, vi_fmt("%.*s", VT(vi_regs[i])))) return 1;
    return 0;
}

static int vi_is_clock(const gram_node *id)
{
    int i;
    if (vi_is(id, "clk") || vi_is(id, "clock")) return 1;   /* named as one, even if only gates use it */
    for (i = 0; i < vi_nclk; i++) if (vi_clk[i]->len == id->len && vi_is(id, vi_fmt("%.*s", VT(vi_clk[i])))) return 1;
    return 0;
}

/* ---- rams: an array signal used like Wire's own ram ------------------------------
 * Wire writes a ram as
 *     process (clk) begin if rising_edge(clk) then if we = '1' then
 *         mem(to_integer(unsigned(addr))) <= din; end if; end if; end process;
 *     q <= mem(to_integer(unsigned(addr)));
 * Read that way it is one ram part again: no register per word, and no mux
 * that has to pick one of 2^n words (which is limited to 32). */

static int vi_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int vi_txteq(const gram_node *a, const gram_node *b)   /* the same text, ignoring case and spaces */
{
    size_t i = 0, j = 0;
    for (;;) {
        while (i < a->len && vi_space(a->text[i])) i++;
        while (j < b->len && vi_space(b->text[j])) j++;
        if (i == a->len || j == b->len) return i == a->len && j == b->len;
        if (vi_lower((unsigned char)a->text[i]) != vi_lower((unsigned char)b->text[j])) return 0;
        i++; j++;
    }
}

static int vi_same_id(const gram_node *a, const gram_node *b)
{
    return a->rule == VHDL_R_ident && a->len == b->len && vi_is(a, vi_fmt("%.*s", VT(b)));
}

/* the one statement in a list (labels, null and assert aside), or NULL */
static gram_node *vi_only_stmt(gram_node *first)
{
    gram_node *k, *st = NULL;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_ident || k->rule == VHDL_R_nullst || k->rule == VHDL_R_assert) continue;
        if (st) return NULL;
        st = k;
    }
    return st;
}

/* is process p the ram write of the array called name? */
static int vi_ram_write(gram_node *p, const gram_node *name, VSym *r)
{
    gram_node *decls, *first = vi_proc_body(p, &decls), *clk, *rst, *rbody, *cbody, *st, *we = NULL, *tg, *id, *v;
    int neg;
    if (decls && decls->child) return 0;
    if (!vi_proc_shape(first, &clk, &neg, &rst, &rbody, &cbody) || rst) return 0;
    st = vi_only_stmt(cbody->child);
    if (st && st->rule == VHDL_R_ifst) {   /* if we then ... end if, nothing else */
        gram_node *c = st->child, *body;
        if (c->rule == VHDL_R_label) c = c->next;
        body = c->next;
        if (body->next && body->next->rule != VHDL_R_ident) return 0;
        we = c;
        st = vi_only_stmt(body->child);
    }
    if (!st || st->rule != VHDL_R_sassign) return 0;
    tg = st->child;
    id = tg->child;
    if (!vi_same_id(id, name) || !id->next || id->next->rule == VHDL_R_drange) return 0;
    r->rambit = 0;
    if (id->next->next) {   /* mem(i)(0) <= d: a ram of one-bit words */
        long k;
        if (id->next->next->next || id->next->next->rule == VHDL_R_drange || !vi_ceval(id->next->next, &k) || k != 0)
            return 0;
        r->rambit = 1;
    }
    v = st->child->next->rule == VHDL_R_wave ? st->child->next->child : st->child->next;
    if (v->next) return 0;   /* after ... */
    r->ramproc = p; r->ramaddr = id->next; r->ramdin = v; r->ramwe = we; r->ramclk = clk; r->ramneg = neg;
    return 1;
}

/* is every use of name under n a read at addr (or in a sensitivity list)? */
static int vi_ram_uses(const gram_node *n, const gram_node *parent, const gram_node *name, const gram_node *addr,
                       const gram_node *skip)
{
    const gram_node *k;
    if (n == skip) return 1;
    if (n->rule == VHDL_R_ident && vi_same_id(n, name)) {
        if (parent && parent->rule == VHDL_R_sens) return 1;
        if (parent && parent->rule == VHDL_R_named && parent->child == n) return 1;   /* a port called the same */
        if (!addr || !parent || parent->rule != VHDL_R_name || parent->child != n) return 0;
        if (!n->next || n->next->rule != VHDL_R_args || vi_nargs((gram_node *)n->next) != 1) return 0;
        if (n->next->child->rule == VHDL_R_drange || !vi_txteq(n->next->child, addr)) return 0;
        return 1;
    }
    for (k = n->child; k; k = k->next)
        if (!vi_ram_uses(k, n, name, addr, skip)) return 0;
    return 1;
}

/* the array signal s (declared as id, with initial value init): is it a ram? */
static int vi_ram_find(VSym *s, gram_node *id, gram_node *init)
{
    VType *t = &vi_types[s->ut - 1];
    gram_node *k, *found = NULL, *first;
    VSym r;
    long c;
    if (!vi_cur || t->alo != 0 || vi_suffix[0]) return 0;
    if (t->elem.ty != T_SLV && t->elem.ty != T_UNS && t->elem.ty != T_SGN) return 0;
    first = vi_cur->arch->child->next->next;
    for (k = first; k; k = k->next) {
        if (k->rule != VHDL_R_process) continue;
        memset(&r, 0, sizeof r);
        if (vi_ram_write(k, id, &r)) {
            if (found) return 0;   /* written by two processes */
            found = k;
            s->ramproc = r.ramproc; s->ramaddr = r.ramaddr; s->ramdin = r.ramdin;
            s->ramwe = r.ramwe; s->ramclk = r.ramclk; s->ramneg = r.ramneg; s->rambit = r.rambit;
        }
    }
    if (!found) return 0;
    if ((s->rambit && t->elem.w != 1) || vi_ceval(s->ramaddr, &c)
        || !vi_ram_uses(s->ramdin, NULL, id, NULL, NULL) || (s->ramwe && !vi_ram_uses(s->ramwe, NULL, id, NULL, NULL)))
        goto no;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_signal || k->rule == VHDL_R_constant || k->rule == VHDL_R_typedecl
            || k->rule == VHDL_R_compdecl || k->rule == VHDL_R_other || k->rule == VHDL_R_ident || k->rule == VHDL_R_funcdecl)
            continue;
        if (!vi_ram_uses(k, NULL, id, s->ramaddr, found)) goto no;
    }
    s->raminit = init;
    return 1;
no:
    s->ramproc = NULL;
    return 0;
}

/* the ram part, where its process was */
static void vi_ram_emit(VSym *s)
{
    VType *t = &vi_types[s->ut - 1];
    gram_node *at = vi_kid(s->ramproc, VHDL_R_PROCESS) ? vi_kid(s->ramproc, VHDL_R_PROCESS) : s->ramproc;
    VSym *cs = vi_find(s->ramclk);
    VTarget tt;
    VX ix, d;
    VBuf data = { 0, 0, 0 };
    const char *we, *clk;
    int j, n, any = 0;
    ix = vi_gen(s->ramaddr, -1);
    if (ix.ty != T_INTV && ix.ty != T_UNS)
        vi_fail(s->ramaddr, "an index is an integer: write %s(to_integer(...))", s->wname);
    if (ix.w > 16) vi_fail(s->ramaddr, "a ram has up to 16 address bits, and this index has %d", ix.w);
    n = 1 << ix.w;
    memset(&tt, 0, sizeof tt);
    tt.s = s->wname;
    tt.ty = s->rambit ? T_SL : t->elem.ty;
    tt.w = s->rambit ? 1 : t->elem.w;
    d = vi_gen(s->ramdin, tt.w);
    vi_assignable(&tt, &d, s->ramdin);
    we = s->ramwe ? vi_cond(s->ramwe).s : vi_lit(1, 1);
    if (!cs || cs->w != 1 || cs->ty == T_ARR || cs->kind == VK_VAR)
        vi_fail(s->ramclk, "the clock must be a one-bit signal or port");
    clk = s->ramneg ? vi_fmt("~%s", cs->wname) : cs->wname;
    if (s->raminit) {   /* the words it starts with */
        for (j = 0; j < n && j <= t->ahi; j++) {
            VX x = vi_array_elem(t, s->raminit, j, s->raminit);
            if (!x.cst) vi_fail(s->raminit, "a starting value must be a constant");
            if (x.v) any = 1;
            vbf(&data, "%s%lX", j ? " " : "", x.v);
        }
    }
    vi_emit(at, "%s = ram(%s, %s, %s, %s%s%s%s)", s->wname, ix.s, d.s, we, clk,
            any ? ", data = \"" : "", any ? data.s : "", any ? "\"" : "");
}

static void vi_statement(gram_node *st)
{
    if (st->rule == VHDL_R_process) {   /* a ram's write: the ram part */
        int i;
        for (i = 0; i < vi_nsyms; i++)
            if (vi_syms[i].ramproc == st) { vi_ram_emit(&vi_syms[i]); return; }
    }
    switch (st->rule) {
    case VHDL_R_condassign: vi_condassign(st); break;
    case VHDL_R_selassign: vi_selassign(st); break;
    case VHDL_R_assert: break;   /* only for simulation */
    case VHDL_R_process: vi_process(st); break;
    case VHDL_R_generate: vi_generate(st); break;
    case VHDL_R_inst: vi_inst(st); break;
    default:
        vi_fail(st, "this kind of statement is not supported yet");
    }
}

/* ---- the design ---------------------------------------------------------------- */

static void vi_ports(gram_node *ports, int is_generic, int emit)
{
    gram_node *d;
    for (d = ports ? ports->child : NULL; d; d = d->next) {
        gram_node *ids = d->child, *mode = vi_kid(d, VHDL_R_mode), *ty = vi_kid(d, VHDL_R_type), *id, *def;
        VSym proto;
        int out = 0;
        VBuf b = { 0, 0, 0 };
        memset(&proto, 0, sizeof proto);
        def = ty->next;
        if (is_generic) {
            vi_type(ty, &proto, 1);
            if (proto.ty != T_INT) vi_fail(ty, "Wire reads integer generics only");
            if (!def) vi_fail(ids, "give %.*s a default value (:= 4); Wire builds the entity with its defaults",
                              VT(ids->child));
            for (id = ids->child; id; id = id->next) {
                VSym *s = vi_declare(id, VK_GEN);
                int k = 0, j;
                s->ty = T_INT;
                s->ival = vi_const_int(def, "a generic's default");
                for (j = 0; j < vi_nsyms - 1; j++) if (vi_syms[j].kind == VK_GEN) k++;
                if (vi_over) s->ival = vi_over[k];   /* a copy for a generic map */
            }
            continue;
        }
        if (mode && (vi_is(mode, "inout") || vi_is(mode, "linkage")))
            vi_fail(mode, "%.*s ports are not supported yet; use in or out", VT(mode));
        out = mode && (vi_is(mode, "out") || vi_is(mode, "buffer"));
        vi_type(ty, &proto, 0);
        {
            VBuf c = { 0, 0, 0 };
            for (id = ids->child; id; id = id->next) {
                VSym *s = vi_declare(id, out ? VK_OUT : VK_IN);
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                s->def = def;
                if (!out && s->w == 1 && !vi_vec(s->ty) && vi_is_clock(id)) { vbf(&c, "%s%s", c.n ? ", " : "", s->wname); continue; }
                if (s->w > 1 || vi_vec(s->ty) || (out && vi_is_reg(id))) vbf(&b, "%s%s[%d]", b.n ? ", " : "", s->wname, s->w);
                else vbf(&b, "%s%s", b.n ? ", " : "", s->wname);
            }
            if (emit && c.n) vi_emit(ids, "clock %s", c.s);   /* a clock of a process: Run and Step on the page */
            if (emit && b.n) vi_emit(ids, "%s %s", out ? "output" : "input", b.s);
        }
    }
}

static void vi_decls(gram_node *first)   /* signal and constant declarations, from first on */
{
    gram_node *d;
    for (d = first; d; d = d->next) {
        gram_node *id;
        VSym proto;
        if (d->rule == VHDL_R_signal || d->rule == VHDL_R_variable) {
            int var = d->rule == VHDL_R_variable;
            gram_node *ty = vi_kid(d, VHDL_R_type), *init = ty->next;
            VBuf b = { 0, 0, 0 };
            memset(&proto, 0, sizeof proto);
            vi_type(ty, &proto, 2);
            if (var && proto.ty == T_ARR) vi_fail(ty, "array variables are not supported yet; use a signal");
            for (id = d->child->child; id; id = id->next) {
                VSym *s = vi_declare(id, var ? VK_VAR : VK_SIG);
                int si = (int)(s - vi_syms);
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                s->ut = proto.ut;
                if (s->ty == T_ARR && !var && vi_ram_find(s, id, init)) {   /* one ram part */
                    VType *t = &vi_types[proto.ut - 1];
                    size_t L = strlen(vi_syms[si].wname);
                    /* Wire writes its ram called mem as mem_words: it is mem again */
                    if (L > 6 && !strcmp(vi_syms[si].wname + L - 6, "_words")) {
                        char *w = vi_fmt("%.*s", (int)(L - 6), vi_syms[si].wname);
                        if (!vi_wire_word(w, (int)strlen(w)) && !vi_wname_used(w)) vi_syms[si].wname = w;
                    }
                    vbf(&b, "%s%s[%d]", b.n ? ", " : "", vi_syms[si].wname, s->rambit ? 1 : t->elem.w);
                    continue;
                }
                if (s->ty == T_ARR) {   /* a signal per element: mem_0, mem_1 ... */
                    VType *t = &vi_types[proto.ut - 1];
                    int k;
                    vi_syms[si].elem0 = vi_nsyms;
                    for (k = t->alo; k <= t->ahi; k++) {
                        VSym *e;
                        char *w = vi_fmt("%s_%d", vi_syms[si].wname, k);
                        while (vi_wname_used(w)) w = vi_fmt("%s_", w);
                        if (vi_nsyms == vi_cap) {
                            vi_cap = vi_cap ? vi_cap * 2 : 64;
                            vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms);
                        }
                        e = &vi_syms[vi_nsyms++];
                        *e = t->elem;
                        e->kind = VK_SIG;
                        e->name = vi_syms[si].name;
                        e->nlen = -1;   /* found through the array, not by name */
                        e->wname = w;
                        e->at = id;
                        vbf(&b, "%s%s[%d]", b.n ? ", " : "", w, e->w);
                    }
                    continue;
                }
                if (init) {   /* the value it starts with: a register without a reset starts there */
                    VTarget t;
                    VX x;
                    memset(&t, 0, sizeof t);
                    t.s = vi_syms[si].wname; t.ty = proto.ty; t.w = proto.w; t.ut = proto.ut;
                    x = vi_gen(init, proto.w);
                    if (!x.cst) vi_fail(init, "a starting value must be a constant");
                    vi_assignable(&t, &x, init);
                    vi_syms[si].bits = x.v;
                    vi_syms[si].hasinit = 1;
                }
                if (var) continue;   /* a variable is only a value inside its process */
                /* always with its width: a register's width cannot be inferred from itself */
                vbf(&b, "%s%s[%d]", b.n ? ", " : "", vi_syms[si].wname, proto.w);
            }
            if (b.n) vi_emit(d->child, "wire %s", b.s);
        } else if (d->rule == VHDL_R_typedecl) {
            VType t;
            gram_node *def = d->child->next;
            memset(&t, 0, sizeof t);
            t.id = d->child;
            if (def->rule == VHDL_R_enumdef) {
                gram_node *l;
                t.lits = def->child;
                for (l = def->child; l; l = l->next) t.n++;
                t.w = 1;
                while ((1 << t.w) < t.n) t.w++;
            } else {
                gram_node *r = def->child;
                long a = vi_const_int(r->child, "an array's range"), z = vi_const_int(r->child->next->next, "an array's range");
                t.isarr = 1;
                t.alo = (int)(a < z ? a : z);
                t.ahi = (int)(a < z ? z : a);
                if (t.ahi - t.alo + 1 > 4096) vi_fail(r, "Wire handles arrays of up to 4096 elements");
                vi_type(r->next, &t.elem, 0);
                if (t.elem.ty == T_ARR) vi_fail(r->next, "arrays of arrays are not supported yet");
            }
            vi_types = (VType *)realloc(vi_types, (size_t)(vi_ntypes + 1) * sizeof *vi_types);
            vi_types[vi_ntypes++] = t;
        } else if (d->rule == VHDL_R_constant) {
            gram_node *ty = vi_kid(d, VHDL_R_type), *val = ty->next;
            memset(&proto, 0, sizeof proto);
            vi_type(ty, &proto, 1);
            for (id = d->child->child; id; id = id->next) {
                VSym *s;
                if (proto.ty == T_ARR) {   /* a table of constants, a rom when read with a signal */
                    VType *t = &vi_types[proto.ut - 1];
                    int k, n = t->ahi - t->alo + 1;
                    unsigned long *bits = (unsigned long *)calloc((size_t)n, sizeof *bits);
                    for (k = 0; k < n; k++) {
                        VX x = vi_array_elem(t, val, k, val);
                        if (!x.cst) vi_fail(val, "a constant's value must be known: literals and other constants");
                        bits[k] = x.v;
                    }
                    s = vi_declare(id, VK_CONST);
                    s->ty = T_ARR; s->ut = proto.ut; s->w = t->elem.w; s->abits = bits;
                    continue;
                }
                if (proto.ty == T_INT) {
                    long v = vi_const_int(val, "a constant's value");
                    s = vi_declare(id, VK_CONST);
                    s->ty = T_INT; s->ival = v;
                } else {
                    VTarget t;
                    VX x;
                    t.s = vi_fmt("%.*s", VT(id)); t.ty = proto.ty; t.w = proto.w; t.sym = NULL; t.ut = proto.ut;
                    x = vi_gen(val, proto.w);
                    if (!x.cst) vi_fail(val, "a constant's value must be known: literals and other constants");
                    vi_assignable(&t, &x, val);
                    s = vi_declare(id, VK_CONST);
                    s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                    s->ut = proto.ut;
                    s->bits = x.v;
                }
            }
        } else if (d->rule == VHDL_R_funcdecl) {   /* inlined where it is called */
            if (vi_nfuncs == 64) vi_fail(d, "more than 64 functions");
            vi_funcs[vi_nfuncs++] = d;
        } else if (d->rule == VHDL_R_compdecl) {
            /* only used by instances, which say they are not supported */
        } else if (d->rule == VHDL_R_other) {
            gram_node *kw = d->child;
            if (kw->rule == VHDL_R_subprog) vi_fail(kw, "functions and procedures are not supported yet");
            vi_fail(kw, "%.*s declarations are not supported yet", VT(kw));
        } else break;
    }
}

static int vi_operator(const char *x)
{
    return !strcmp(x, "logop") || !strcmp(x, "relop") || !strcmp(x, "addop") || !strcmp(x, "mulop") || !strcmp(x, "shiftop");
}

/* a syntax error, with what was expected in VHDL's words, not the grammar's */
static void vi_syntax(const gram_error *e)
{
    static const char *names[][2] = {
        { "ident", "a name" }, { "number", "a number" }, { "chr", "'0' or '1'" }, { "str", "\"0101\"" },
        { "bitstr", "x\"F0\"" }, { "mode", "in or out" }, { "dir", "downto or to" }, { "logop", "and, or, xor" },
        { "relop", "=, /=, <, >" }, { "addop", "+, -, &" }, { "mulop", "*" }, { "shiftop", "sll, srl" },
        { "selname", "a library name" }, { "otherkw", "signal or constant" }, { "pbody", "end process" },
        { "gbody", "end generate" }, { "subprog", "a function" }, { NULL, NULL } };
    const char *seen[GRAM_MAX_EXPECTED];
    int n = 0, value = 0, ops = 1;
    unsigned i;
    fprintf(stderr, "%s:%lu:%lu: syntax error", vi_file, (unsigned long)e->line, (unsigned long)e->col);
    for (i = 0; i < e->nexpected; i++) {
        const char *x = e->expected[i];
        if (!strcmp(x, "chr") || !strcmp(x, "bitstr") || !strcmp(x, "NOT") || !strcmp(x, "ABS")
            || !strcmp(x, "number") || !strcmp(x, "str")) value = 1;
    }
    if (value) { fputs(": expected a value (a name, a number, '0', \"0101\", not or ( ...)\n", stderr); exit(1); }
    for (i = 0; i < e->nexpected; i++) if (!vi_operator(e->expected[i])) ops = 0;   /* operators only when nothing else fits */
    for (i = 0; i < e->nexpected; i++) {
        const char *x = e->expected[i], *say = NULL;
        char low[32];
        int j, k;
        if (!ops && vi_operator(x)) continue;
        for (j = 0; names[j][0]; j++) if (!strcmp(x, names[j][0])) say = names[j][1];
        if (!say) {   /* a keyword token (WHEN) in lower case, a literal as it is */
            for (k = 0; x[k] && k < 31; k++) low[k] = (char)vi_lower(x[k]);
            low[k] = 0;
            say = low[0] >= 'a' && low[0] <= 'z' ? vi_fmt("%s", low) : x;
        }
        for (j = 0; j < n; j++) if (!strcmp(seen[j], say)) break;
        if (j < n) continue;
        seen[n++] = say;
    }
    for (i = 0; i < (unsigned)n; i++) fprintf(stderr, "%s%s", i == 0 ? ": expected " : i + 1 == (unsigned)n ? " or " : ", ", seen[i]);
    fputc('\n', stderr);
    exit(1);
}

static gram_node *arch_of(gram_node *arch) { return arch->child->next; }   /* architecture a OF e */

/* the Wire, on the VHDL's lines; *len gets its length */
static char *vhdl_to_wire(const char *text, size_t len, const char *file, size_t *outlen)
{
    gram_arena a;
    gram_error err;
    gram_grammar *g;
    gram_node *design, *u, *k;
    int ctx = 0;
    size_t asz = ((size_t)1 << 22) + len * 96, i;
    void *buf = malloc(asz);
    VBuf out = { 0, 0, 0 };
    int ln;
    vi_src = text; vi_len = len; vi_file = file;
    if (!buf) { fputs("wire: out of memory\n", stderr); exit(1); }
    gram_arena_init(&a, buf, asz);
    g = gram_compile(&a, VHDL_GRAMMAR, &err);
    if (!g) { fputs("vhdl grammar:", stderr); gram_error_print(&err, put, stderr); exit(1); }
    design = gram_parse(g, NULL, text, len, &a, &err);
    if (!design) {
        if (err.code != GRAM_E_SYNTAX) { fprintf(stderr, "%s:", file); gram_error_print(&err, put, stderr); exit(1); }
        vi_syntax(&err);
    }
    vi_nlines = 1;
    for (i = 0; i < len; i++) if (text[i] == '\n') vi_nlines++;
    vi_lines = (VBuf *)calloc((size_t)vi_nlines + 2, sizeof *vi_lines);

    for (u = design->child; u; u = u->next) {
        if (u->rule == VHDL_R_use) {
            for (k = u->child; k; k = k->next) {
                size_t j;
                char low[256];
                for (j = 0; j < k->len && j < sizeof low - 1; j++) low[j] = (char)vi_lower(k->text[j]);
                low[j] = 0;
                if (strstr(low, "std_logic_1164")) ctx |= CTX_1164;
                if (strstr(low, "numeric_std") || strstr(low, "std_logic_arith")) ctx |= CTX_NUM;
                if (strstr(low, "std_logic_unsigned")) ctx |= CTX_UNS;
                if (strstr(low, "std_logic_signed")) ctx |= CTX_SGN;
            }
        } else if (u->rule == VHDL_R_entity) {
            gram_node *last = NULL;
            if (vi_ent_named(u->child)) vi_fail(u->child, "there are two entities called %.*s", VT(u->child));
            for (k = u->child->next; k; k = k->next) last = k;
            if (last && last->rule == VHDL_R_ident && !vi_is(last, vi_fmt("%.*s", VT(u->child))))
                vi_fail(last, "the entity is called %.*s, but it ends with end %.*s", VT(u->child), VT(last));
            if (vi_nents && !vi_ents[vi_nents - 1]->arch)
                vi_fail(u->child, "%.*s has no architecture before the next entity; put each architecture right "
                                  "after its entity", VT(vi_ents[vi_nents - 1]->ent->child));
            vi_ents = (VEnt **)realloc(vi_ents, (size_t)(vi_nents + 1) * sizeof *vi_ents);
            vi_ents[vi_nents] = (VEnt *)calloc(1, sizeof **vi_ents);
            vi_ents[vi_nents]->ctx = ctx;   /* the use clauses before it are for it (and its architecture) */
            ctx = 0;
            vi_ents[vi_nents++]->ent = u;
            vi_nbase = vi_nents;
        } else if (u->rule == VHDL_R_arch) {
            VEnt *e = vi_ent_named(arch_of(u));
            if (!e) vi_fail(arch_of(u), "this architecture is of %.*s, but there is no entity called that before it",
                            VT(arch_of(u)));
            if (e != vi_ents[vi_nents - 1]) vi_fail(u->child, "put each architecture right after its entity");
            e->arch = u;   /* several: the last one, as a simulator picks it */
            e->ctx |= ctx;
            ctx = 0;
        }
    }
    if (!vi_nents) vi_fail(NULL, "there is no entity in this file");
    if (!vi_ents[vi_nents - 1]->arch) vi_fail(vi_ents[vi_nents - 1]->ent->child, "%.*s has no architecture",
                                              VT(vi_ents[vi_nents - 1]->ent->child));

    /* which entities are placed by others; the top is one nobody places (the last, if several) */
    for (ln = 0; ln < vi_nbase; ln++) vi_mark_used(vi_ents[ln]->arch->child->next->next);
    {
        int t = -1;
        for (ln = 0; ln < vi_nbase; ln++) if (!vi_ents[ln]->used) t = ln;
        if (t < 0) vi_fail(vi_ents[vi_nbase - 1]->ent->child, "every entity is used by another one, so none is the top");
        vi_ents[t]->top = 1;
    }
    for (ln = 0; ln < vi_nbase; ln++) {
        VEnt *e = vi_ents[ln];
        e->wname = vi_unique(vi_fmt("%.*s", VT(e->ent->child)));
        vi_ent_ports(e);
    }

    /* the entities on their own lines; then the copies for generic maps (which
     * can make more copies) after the last line */
    for (ln = 0; ln < vi_nbase; ln++) vi_ent_emit(vi_ents[ln]);
    for (ln = vi_nbase; ln < vi_nents; ln++) vi_ent_emit(vi_ents[ln]);

    for (ln = 1; ln <= vi_nlines; ln++) {
        if (vi_lines[ln].n) vb_add(&out, vi_lines[ln].s, vi_lines[ln].n);
        if (ln < vi_nlines) vb_add(&out, "\n", 1);
    }
    if (vi_extra.n) vb_add(&out, vi_extra.s, vi_extra.n);
    if (!out.s) vb_add(&out, "", 0);
    free(buf);
    *outlen = out.n;
    return out.s;
}

/* the VHDL line a line of the Wire comes from (the same, except in the copies
 * made for generic maps, which come after the VHDL's last line) */
static int vhdl_line(int wline)
{
    int k = wline - vi_nlines - 1;
    return k >= 0 && k < vi_nx ? vi_xline[k] : wline;
}

/* the same Wire without the empty lines and the padding, one statement a line */
static void vhdl_print_wire(FILE *f, const char *w)
{
    while (*w) {
        const char *e = strchr(w, '\n'), *p;
        size_t n = e ? (size_t)(e - w) : strlen(w);
        while (n && *w == ' ') { w++; n--; }
        for (p = w; n; ) {
            const char *semi = memchr(p, ';', n);
            size_t m = semi ? (size_t)(semi - p) : n;
            fprintf(f, "%.*s\n", (int)m, p);
            if (!semi) break;
            n -= m + 1; p = semi + 1;
            while (n && *p == ' ') { p++; n--; }
        }
        if (!e) break;
        w = e + 1;
    }
}
