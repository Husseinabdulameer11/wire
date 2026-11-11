/* wire_vhdl_in.c - VHDL in: reads a VHDL file and turns it into Wire, which
 * wire.c then checks, lays out and simulates like any other circuit.
 * Included by wire.c, after wire_vhdl.c (it uses its VBuf).
 *
 *   wire adder.vhd               writes adder.html
 *   wire --to-wire adder.vhd     prints the Wire it was turned into
 *
 * What it reads: entities and their architectures, packages (and their
 * bodies) named in use work.p.all, with generics (their default values),
 * std_logic, std_logic_vector, unsigned, signed, integer, boolean and record
 * ports (in, out, buffer, inout) and signals, constants, subtypes and
 * aliases, and the concurrent statements: assignments, when/else,
 * with/select, procedure calls. Expressions: and or xor nand nor xnor not,
 * = /= < <= > >=, + - * / mod rem ** abs (numeric_std, or
 * std_logic_unsigned), &, indexes and slices, aggregates like
 * (others => '0'), the conversions unsigned() signed() std_logic_vector()
 * to_unsigned() to_signed() to_integer() resize(), and shifts by a constant
 * or a signal. 'Z' given to a signal is a tri(), and a signal several
 * statements drive is a bus; an inout port is a pin each way.
 * Every entity but the top one becomes a Wire component (a chip), and
 * port map places it (a port whole, in parts, or field by field), so chips
 * can be built from chips. The top entity is the one no other entity uses
 * (the last one, if there are several).
 * for ... generate and if ... generate are unrolled (labels and signals
 * inside get _0, _1 ... on the end). Processes are read statement by
 * statement (if, case, for with exit and return, variables, procedure
 * calls): a clocked one (if rising_edge(clk), with or without an asynchronous
 * reset first, or wait until rising_edge(clk)) becomes registers (dff) with
 * the logic in front of them, and what it gives values to outside the edge
 * is logic; any other process becomes logic, and a signal it does not give a
 * value on every path is refused (it would be a latch). Functions and
 * procedures are inlined where they are called. Enumerations (type state is
 * (idle, run)) are numbered in binary; arrays (one or two indexes) become a
 * signal (or variable) per element, read with a mux, or a rom when constant,
 * or rams (one part per byte lane) when written like rams; records a signal
 * per field; integers with a range become unsigned (signed when the range
 * has negative values). Values wider than 32 bits are kept in chunks of 32,
 * and a big process beside others is written as a chip of its own.
 *
 * The Wire keeps the VHDL's lines: each statement lands on the line (and
 * near the column) it came from, so every error Wire finds later, and every
 * part's line on the page, points at the right line of the VHDL. */
#include "vhdl_grammar.h"

enum { T_SL, T_BOOL, T_SLV, T_UNS, T_SGN, T_STR, T_INT, T_ENUM, T_ARR, T_INTV, T_REC };
static const char *vi_tyname[] = { "std_logic", "boolean", "std_logic_vector", "unsigned", "signed",
                                   "a literal", "integer", "a state (an enumeration)", "an array", "integer", "a record" };
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
    char *zd, *ze;               /* a register given 'Z': its data and its enable, a register each */
    /* an inout port: it is driven through wname (the chip's output pin, or a bus
     * at the top) and read through rname (the chip's input pin p_in: the bus
     * outside, all its drivers together); at the top, the page drives it too
     * through the inputs rname (p_in) and roe (p_oe) */
    int inout;
    char *rname, *roe;
    /* an array signal written as a ram (one clocked process: if we then
     * mem(to_integer(addr)) <= din) and only read as mem(to_integer(addr)):
     * it becomes one ram part, not a register per element */
    gram_node *ramproc, *ramaddr, *ramclk, *raminit;
    gram_node *ramrst;           /* its process has a reset first: no writes while it is on */
    int ramneg;
    struct VLane *lanes;         /* its writes: one ram part each (byte lanes: mem(i)(7 downto 0) <= ...) */
    int nlanes;
    int screenw;                 /* attribute wire_screen of it: a screen this wide */
    int screentext;              /* attribute wire_text of it: the screen shows characters */
    int wch0, wnch;              /* wider than 32 bits: a signal (or variable) per 32 bits, from wch0 */
    int fld0;                    /* a record: the VSym of its first field (the others follow, in order) */
    int alias;                   /* found by name, it is VSym alias - 1 (a procedure's signal parameter) */
    gram_node *aliased;          /* alias x is <name>: reading x reads that */
    int sigparam;                /* a procedure's out parameter given a value with <= */
    int intneg;                  /* an integer with negative values: kept as signed bits */
    int ndrv;                    /* the statements that gave it a whole value so far */
    const gram_node *plain;      /* one of them that always drives it (no 'Z'), if any */
} VSym;

/* one write of a ram: the bits of the word it gives (hi..lo, counted from 0), the
 * value, and the ifs around it (all must hold); its ram part is called wname */
#define VI_MAXCOND 8
typedef struct VLane {
    int hi, lo, bit;             /* bit: written as mem(i)(k), one std_logic */
    gram_node *din, *cond[VI_MAXCOND];
    int ncond;
    char *wname;
} VLane;

/* a type of the architecture: an enumeration or an array */
typedef struct {
    const gram_node *id;
    int isarr;
    gram_node *lits;             /* enumeration: the first literal */
    int n, w;                    /* enumeration: how many literals, and bits */
    VSym elem;                   /* array: the element type */
    int alo, ahi;                /* array: the lowest and highest index */
    int adown;                   /* array: declared with downto */
    int blo, bhi, two;           /* array (a to b, c to d): the second index; elements go row by row */
    int unbound;                 /* array (natural range <>): the range comes with each signal */
    int anon;                    /* made for one signal's range: not found by name */
    int issub;                   /* subtype x is ...: subnode is the type it is */
    gram_node *subnode;
    int isint;                   /* type x is range a to b: an integer in ilo..ihi */
    long ilo, ihi;
    int isrec;                   /* record: nf fields, named fid[], of type fty[] */
    int nf;
    gram_node **fid;
    VSym *fty;
} VType;

/* an entity, its architecture and its ports (as seen from outside) */
typedef struct {
    gram_node *ent, *arch;
    int used, top, done, ctx;
    int tb;                      /* no ports: a testbench, for simulation only; not read */
    char *wname;                 /* its name in Wire */
    VSym *ports, *gens;          /* gens: the generics, with their values */
    int nports, ngens;
    long *over;                  /* a copy made for a generic map: the value of each generic */
    int pkgs[16];                /* the packages its use clauses name (use work.p.all) */
    int npkgs;
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
    /* wider than 32 bits (w > MAXW): no s, but chunks of 32 bits, bits 0-31
     * first (the last may be narrower), each a Wire expression that can be
     * sliced; chv their values when every bit is known */
    int nch;
    char **ch;
    unsigned long *chv;
    char *xb;                    /* a wide value that is xb (xbw bits) with its sign (xsgn) or 0s on top */
    int xbw, xsgn;
    /* tri-state: z, every bit is 'Z' (nothing drives it); zen, a 1-bit Wire
     * expression: the value is driven only while zen is 1 (tri(s, zen)) */
    int z;
    char *zen;
    int zmix;                    /* joined from parts where some are 'Z' and some not */
} VX;

static VX vi_wbits(const char *bits, int w, int ty);
static VX vi_wide_read(VSym *s, gram_node *at);

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
/* the packages in the file: the package, its body, the use clauses before it
 * (what of ieee it sees, and the packages it uses) */
typedef struct { gram_node *spec, *body; int ctx, ndeps, mark; int deps[16]; } VPkg;
static VPkg vi_pkgs[64];
static int vi_npkgs;
static int vi_to_extra;          /* vi_emitp writes there, a statement a line */
static int *vi_xline, vi_nx;     /* the VHDL line each of those lines comes from */
static const long *vi_over;      /* while a copy is read: its generics' values */
static VEnt *vi_cur;             /* the entity being turned into Wire */
static char vi_suffix[128];      /* inside generate: _3 (or _3_1 nested) for names declared there */
                                 /* vi_types: the enumerations and arrays declared so far */
static int vi_ntypes;
static gram_node *vi_funcs[256]; /* the functions and procedures declared so far: inlined where they are called */
static int vi_fctx[256];         /* the use clauses each is read with (a package's own) */
static int vi_nfuncs;

#define VT(n) (int)(n)->len, (n)->text

/* a declaration (in an architecture, a generate or a package), not a statement */
static int vi_is_decl(int r)
{
    return r == VHDL_R_signal || r == VHDL_R_constant || r == VHDL_R_typedecl || r == VHDL_R_subtype || r == VHDL_R_alias
        || r == VHDL_R_compdecl || r == VHDL_R_funcdecl || r == VHDL_R_procdecl || r == VHDL_R_subspec
        || r == VHDL_R_attrdecl || r == VHDL_R_attrspec || r == VHDL_R_other;
}

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
    for (i = 0; i < vi_nsyms; i++) {
        const VSym *s = &vi_syms[i];
        if (s->wname && !strcmp(s->wname, w)) return 1;
        if ((s->rname && !strcmp(s->rname, w)) || (s->roe && !strcmp(s->roe, w))
            || (s->zd && !strcmp(s->zd, w)) || (s->ze && !strcmp(s->ze, w))) return 1;
    }
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

/* ---- records: a symbol per field, named r.f (a name VHDL cannot write, so
 * r.f in the VHDL is found by turning it into one identifier) ---------------- */

static VType *vi_types;
static gram_node *vi_ident_of(const char *text, gram_node *next)   /* an identifier node for text */
{
    gram_node *n = (gram_node *)calloc(1, sizeof *n);
    n->rule = VHDL_R_ident;
    n->text = text;
    n->len = strlen(text);
    n->next = next;
    return n;
}

/* the fields of record s (declared as s->name), each its own symbol, of kind kind
 * and named in Wire base_field; nested records in turn. Returns how many. */
static void vi_rec_fields(int si, int kind, VBuf *wires, int emit)
{
    VType *t = &vi_types[vi_syms[si].ut - 1];
    int k, first = vi_nsyms;
    vi_syms[si].fld0 = first;
    for (k = 0; k < t->nf; k++) {   /* all the fields first, so they follow each other */
        VSym *e;
        char *w = vi_fmt("%s_%.*s", vi_syms[si].wname, VT(t->fid[k]));
        while (vi_wname_used(w)) w = vi_fmt("%s_", w);
        if (vi_nsyms == vi_cap) { vi_cap = vi_cap ? vi_cap * 2 : 64; vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms); }
        e = &vi_syms[vi_nsyms++];
        *e = t->fty[k];
        e->kind = kind;
        e->name = vi_fmt("%.*s.%.*s", vi_syms[si].nlen, vi_syms[si].name, VT(t->fid[k]));
        e->nlen = (int)strlen(e->name);
        e->wname = w;
        e->at = vi_syms[si].at;
        e->def = NULL;
    }
    for (k = 0; k < t->nf; k++) {
        VSym *e = &vi_syms[first + k];
        if (e->ty == T_REC) { vi_rec_fields(first + k, kind, wires, emit); continue; }
        if (emit && wires) vbf(wires, "%s%s[%d]", wires->n ? ", " : "", e->wname, e->w);
    }
}

/* r.f.g as one identifier node (with what follows it: r.f(3) is that, then (3)); NULL if n is not one */
static gram_node *vi_unfield(gram_node *id)
{
    VBuf b = { 0, 0, 0 };
    gram_node *k;
    if (!id->next || id->next->rule != VHDL_R_field) return NULL;
    vbf(&b, "%.*s", VT(id));
    for (k = id->next; k && k->rule == VHDL_R_field; k = k->next) vbf(&b, ".%.*s", VT(k->child));
    return vi_ident_of(b.s, k);
}

static VSym *vi_find(const gram_node *id);
static void vi_fail(const gram_node *at, const char *f, ...);

/* field k of a record value: another record (by name), or an aggregate (a => x, others => y) */
static gram_node *vi_rec_part(gram_node *v, const VType *rt, int k)
{
    const gram_node *fid = rt->fid[k];
    if (v->rule == VHDL_R_ident || (v->rule == VHDL_R_name && v->child->next && v->child->next->rule == VHDL_R_field)) {
        gram_node *one = v->rule == VHDL_R_ident ? v : vi_unfield(v->child);
        VSym *r = one && !one->next ? vi_find(one) : NULL;
        if (!r || r->ty != T_REC || &vi_types[r->ut - 1] != rt) vi_fail(v, "this is not a record of the same type");
        return vi_ident_of(vi_fmt("%.*s.%.*s", (int)one->len, one->text, VT(fid)), NULL);
    }
    if (v->rule == VHDL_R_aggregate) {
        gram_node *el, *others = NULL;
        int pos = 0;
        for (el = v->child; el; el = el->next) {
            gram_node *ch = el->child->rule == VHDL_R_choices ? el->child : NULL, *e = ch ? ch->next : el->child, *c;
            if (!ch) { if (pos++ == k) return e; continue; }
            for (c = ch->child; c; c = c->next) {
                if (c->rule == VHDL_R_OTHERS) others = e;
                else if (c->rule == VHDL_R_ident && c->len == fid->len && !strncmp(c->text, fid->text, 0)) {
                    size_t j;
                    for (j = 0; j < c->len && (c->text[j] | 32) == (fid->text[j] | 32); j++) {}
                    if (j == c->len) return e;
                }
            }
        }
        if (others) return others;
        vi_fail(v, "field %.*s of this record has no value; add %.*s => ...", VT(fid), VT(fid));
    }
    vi_fail(v, "a record is given a value from another record or an aggregate, like (a => '0', b => x)");
    return NULL;
}

/* ---- output: Wire on the VHDL's lines ---------------------------------- */

/* a process being read: what it makes is kept here, then written as a chip
 * of its own (when it is big) or where it is */
typedef struct { const char *at; char *s; } VCap;
static VCap *vi_caps;
static int vi_ncaps, vi_capsz, vi_capturing;

static void vi_emitp(const char *at, const char *f, ...)
{
    va_list ap;
    size_t line = 1, col = 1;
    char tmp[256], *s;
    int n;
    VBuf *b;
    if (vi_capturing) {
        va_start(ap, f);
        n = vsnprintf(NULL, 0, f, ap);
        va_end(ap);
        s = (char *)malloc((size_t)n + 1);
        va_start(ap, f);
        vsnprintf(s, (size_t)n + 1, f, ap);
        va_end(ap);
        if (vi_ncaps == vi_capsz) {
            vi_capsz = vi_capsz ? vi_capsz * 2 : 64;
            vi_caps = (VCap *)realloc(vi_caps, (size_t)vi_capsz * sizeof *vi_caps);
        }
        vi_caps[vi_ncaps].at = at;
        vi_caps[vi_ncaps++].s = s;
        return;
    }
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

static VType *vi_find_type(const gram_node *id, int *index);

static int vi_attr(const gram_node *id, const gram_node *at, long *out)
{
    VSym *s = vi_find(id);
    if (s && s->ty == T_ARR && s->ut) {   /* an array: its index range */
        VType *t = vi_find_type(NULL, &(int){ s->ut - 1 });
        int lo = t->alo, hi = t->ahi;
        if (vi_is(at, "length")) *out = hi - lo + 1;
        else if (vi_is(at, "high")) *out = hi;
        else if (vi_is(at, "low")) *out = lo;
        else if (vi_is(at, "left")) *out = t->adown ? hi : lo;
        else if (vi_is(at, "right")) *out = t->adown ? lo : hi;
        else return 0;
        return 1;
    }
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

/* a range: a to b, a downto b, or x'range / x'reverse_range */
static void vi_range(const gram_node *r, long *from, long *to, int *down, const char *what)
{
    if (r->rule == VHDL_R_rangeof) {
        const gram_node *n = r->child, *at;
        long lf = 0, rt = 0;
        int rev;
        if (n->rule != VHDL_R_name || !n->child->next || n->child->next->rule != VHDL_R_attr || n->child->next->next)
            vi_fail(r, "%s is a to b, a downto b or x'range", what);
        at = n->child->next->child;
        rev = vi_is(at, "reverse_range");
        if (!rev && !vi_is(at, "range")) vi_fail(at, "%s is a to b, a downto b, x'range or x'reverse_range", what);
        if (!vi_attr(n->child, &(gram_node){ .text = "left", .len = 4 }, &lf) || !vi_attr(n->child, &(gram_node){ .text = "right", .len = 5 }, &rt))
            vi_fail(n->child, "%.*s has no range Wire knows", VT(n->child));
        *down = lf > rt || (lf == rt && vi_find(n->child) && vi_find(n->child)->down);
        if (rev) { long t = lf; lf = rt; rt = t; *down = !*down; }
        *from = lf; *to = rt;
        return;
    }
    *from = vi_const_int(r->child, what);
    *to = vi_const_int(r->child->next->next, what);
    *down = vi_is(r->child->next, "downto");
}

/* ---- types --------------------------------------------------------------- */

/* type -> ty, width and range */
static VType *vi_find_type(const gram_node *id, int *index)
{
    int i;
    if (!id) return &vi_types[*index];   /* by its index */
    for (i = vi_ntypes - 1; i >= 0; i--)
        if (!vi_types[i].anon && vi_is(id, vi_fmt("%.*s", (int)vi_types[i].id->len, vi_types[i].id->text))) { if (index) *index = i; return &vi_types[i]; }
    return NULL;
}

#define VI_MAXWIDE 256           /* signals and variables wider than 32 bits: a Wire wire per 32 */
#define VI_MAXCH (VI_MAXWIDE / 32)   /* the most chunks of 32 a value has */

static int vi_nelems(const VType *t)   /* an array's elements, both indexes counted */
{
    return (t->ahi - t->alo + 1) * (t->two ? t->bhi - t->blo + 1 : 1);
}
static int vi_wide_ok;           /* the type being read may be that wide */

/* allow_int: 0 no integers, 1 integer constants, 2 integer signals and variables (with a range) */
static void vi_type(gram_node *t, VSym *s, int allow_int)
{
    gram_node *id = t->child, *r = id->next;
    int ti;
    VType *ut = vi_find_type(id, &ti);
    if (ut) {
        if (ut->isint) {   /* type small is range 0 to 15: an integer with that range */
            if (r) vi_fail(r, "%.*s has a range already", VT(id));
            if (!allow_int) vi_fail(id, "Wire needs a width here: use unsigned(n-1 downto 0) instead of %.*s", VT(id));
            if (allow_int == 1) { s->ty = T_INT; s->w = 0; return; }
            if (ut->ilo < 0) {   /* negative values too: signed bits, enough for both ends */
                s->ty = T_SGN; s->w = 1; s->intneg = 1;
                while (s->w < 32 && ((1L << (s->w - 1)) < -ut->ilo || (1L << (s->w - 1)) <= ut->ihi)) s->w++;
            } else {
                s->ty = T_INTV; s->w = 1;
                while (s->w < 31 && (1L << s->w) <= ut->ihi) s->w++;
            }
            s->left = s->w - 1; s->right = 0; s->down = 1;
            return;
        }
        if (ut->issub) {   /* subtype byte is std_logic_vector(7 downto 0): that type, read here */
            if (r) vi_fail(r, "%.*s has a range already", VT(id));
            vi_type(ut->subnode, s, allow_int);
            return;
        }
        if (ut->isrec) {
            if (r) vi_fail(r, "%.*s is a record and has no range", VT(id));
            s->ty = T_REC; s->ut = ti + 1; s->w = 0;
            {
                int k;
                for (k = 0; k < ut->nf; k++) s->w += ut->fty[k].w;
            }
            return;
        }
        if (ut->isarr && ut->unbound) {   /* mem(0 to 15) of an array type without a range: a type for it */
            VType t = *ut;
            long a, z;
            int down;
            if (!r) vi_fail(id, "%.*s has no range of its own: give it one, like %.*s(0 to 15)", VT(id), VT(id));
            vi_range(r, &a, &z, &down, "an array's range");
            t.unbound = 0; t.anon = 1;
            t.alo = (int)(a < z ? a : z); t.ahi = (int)(a < z ? z : a); t.adown = down;
            if (t.ahi - t.alo + 1 > 4096) vi_fail(r, "Wire handles arrays of up to 4096 elements");
            vi_types = (VType *)realloc(vi_types, (size_t)(vi_ntypes + 1) * sizeof *vi_types);
            vi_types[vi_ntypes++] = t;
            s->ut = vi_ntypes; s->ty = T_ARR; s->w = t.elem.w;
            return;
        }
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
            if (hi < lo) vi_fail(r, "this range is empty");
            if (lo < 0) {   /* negative values too: signed bits, enough for both ends */
                s->ty = T_SGN; s->w = 1; s->intneg = 1;
                while (s->w < 32 && ((1L << (s->w - 1)) < -lo || (1L << (s->w - 1)) <= hi)) s->w++;
                s->left = s->w - 1; s->right = 0; s->down = 1;
                return;
            }
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
    if (!r || (r->rule != VHDL_R_drange && r->rule != VHDL_R_rangeof)) vi_fail(id, "give %.*s a range, like %.*s(7 downto 0)", VT(id), VT(id));
    if (r->next) vi_fail(r->next, "%.*s has one range", VT(id));
    {
        long a, z;
        vi_range(r, &a, &z, &s->down, "a range");
        s->left = (int)a; s->right = (int)z;
    }
    s->w = s->down ? s->left - s->right + 1 : s->right - s->left + 1;
    if (s->w < 1) vi_fail(r, "this range is empty");
    if (s->w > (vi_wide_ok ? VI_MAXWIDE : MAXW))
        vi_fail(r, vi_wide_ok ? "Wire handles signals of up to %d bits, and this is %d" :
                   "Wire handles up to %d bits here, and this is %d; wider values are not supported yet (signals and variables can be up to 256 bits)",
                vi_wide_ok ? VI_MAXWIDE : MAXW, s->w);
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

/* 'Z' is a value only where a signal is given one (y <= ... else (others => 'Z')):
 * vi_zok is set just before that value is read, and taken by the first vi_gen_any */
static int vi_zok;

static void vi_no_z(const gram_node *at)
{
    vi_fail(at, "'Z' (nothing driving) can only be the whole value given to a signal, "
                "like y <= x when oe = '1' else (others => 'Z')");
}

/* every bit of w is 'Z' */
static VX vi_zval(int w, int ty)
{
    VX x;
    memset(&x, 0, sizeof x);
    x.ty = ty; x.w = w; x.atom = 2; x.z = 1;
    if (w > 32) {
        int i;
        x.nch = (w + 31) / 32;
        x.ch = (char **)calloc((size_t)x.nch, sizeof *x.ch);
        x.chv = (unsigned long *)calloc((size_t)x.nch, sizeof *x.chv);
        for (i = 0; i < x.nch; i++) x.ch[i] = vi_lit(0, w - 32 * i < 32 ? w - 32 * i : 32);
    } else x.s = vi_lit(0, w);
    return x;
}

static int vi_bitchar(const gram_node *at, int c)
{
    c = vi_lower(c);
    if (c == '1' || c == 'h') return 1;
    if (c == '0' || c == 'l' || c == '-' || c == 'x' || c == 'u' || c == 'w') return 0;
    if (c == 'z') return -1;
    vi_fail(at, "'%c' is not a value of std_logic", c);
    return 0;
}

static VX vi_strlit(const gram_node *n)
{
    unsigned long v = 0;
    size_t i;
    int w = (int)n->len - 2, nz = 0;
    if (w < 1) vi_fail(n, "an empty string is not a value");
    for (i = 1; i + 1 < n->len; i++) if (vi_lower(n->text[i]) == 'z') nz++;
    if (nz) {   /* "ZZZZ": nothing drives it */
        if (!vi_zok) vi_no_z(n);
        if (nz != w) vi_fail(n, "%.*s has some bits 'Z' and some not; give the 'Z' bits and the others "
                                "to slices of the signal in two assignments", VT(n));
        if (w > VI_MAXWIDE) vi_fail(n, "Wire handles up to %d bits, and this is %d", VI_MAXWIDE, w);
        return vi_zval(w, T_STR);
    }
    if (w > VI_MAXWIDE) vi_fail(n, "Wire handles up to %d bits, and this is %d", VI_MAXWIDE, w);
    if (w > MAXW) {
        char *bits = (char *)malloc((size_t)w);
        for (i = 1; i + 1 < n->len; i++) bits[i - 1] = vi_bitchar(n, n->text[i]) ? '1' : '0';
        return vi_wbits(bits, w, T_STR);
    }
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
    if (base) {   /* wider than 32 bits: the bits one by one */
        size_t j;
        int nb = 0;
        for (j = i + 2; j + 1 < n->len; j++) if (n->text[j] != '_') nb += base;
        if ((width < 0 ? nb : width) > MAXW) {
            int W = width < 0 ? nb : (int)width, q = 0, d;
            char *bits = (char *)malloc((size_t)(nb > W ? nb : W) + 1);
            if (W > VI_MAXWIDE) vi_fail(n, "Wire handles up to %d bits, and this is %d", VI_MAXWIDE, W);
            for (j = i + 2; j + 1 < n->len; j++) {
                int t, cc = vi_lower(n->text[j]);
                if (cc == '_') continue;
                d = cc >= 'a' ? cc - 'a' + 10 : cc - '0';
                if (d >= (1 << base)) vi_fail(n, "%.*s has a digit that does not belong", VT(n));
                for (t = base - 1; t >= 0; t--) bits[q++] = (char)('0' + ((d >> t) & 1));
            }
            if (nb > W) {   /* the extra digits on top must be 0 */
                int t;
                for (t = 0; t < nb - W; t++) if (bits[t] == '1') vi_fail(n, "%.*s does not fit in %d bits", VT(n), W);
                return vi_wbits(bits + (nb - W), W, T_STR);
            }
            if (nb < W) {
                memmove(bits + (W - nb), bits, (size_t)nb);
                memset(bits, '0', (size_t)(W - nb));
            }
            return vi_wbits(bits, W, T_STR);
        }
    }
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
static VX vi_gen_any(gram_node *n, int want);
static VX vi_var_read(VSym *s, gram_node *at);
static VX vi_array_read(VSym *s, gram_node *id, gram_node *args, int want);
static VX vi_barrel(VX x, VX amount, int left, int arith, int rot, gram_node *at);
static char *vi_muxn(char **d, int n, const char *sel, int selw, int w);
static VX vi_mux2(VX d0, VX d1, VX c);
static void vi_bind(VX *x);
static VX vi_absval(VX x);
static VX vi_array_elem(VType *t, gram_node *v, int i, gram_node *at);
static char *vi_ram_word(const VSym *s);

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

static VX vi_wresize(VX x, int w, int sgn);

/* x widened (or cut) to w bits, as unsigned or signed */
static VX vi_resize(VX x, int w, int sgn)
{
    VBuf b = { 0, 0, 0 };
    int i;
    if (x.w == w) return x;
    if (w > MAXW || x.w > MAXW) return vi_wresize(x, w, sgn);
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


/* ---- values wider than 32 bits ------------------------------------------------
 * A Wire wire has up to 32 bits, so a wider VHDL value is kept as chunks of
 * 32 (VX.ch, bits 0-31 first), and a wider signal or variable as one Wire
 * wire (or variable) per chunk: prod(63 downto 0) is prod_31_0 and prod_63_32.
 * Slices, &, resize, the conversions, logic, comparisons, + - and * work on
 * them; anything else says it is not supported yet. */

static int vi_wide(const VX *x) { return x->w > MAXW && x->ty != T_INT; }

static const char *vi_stmt_at;   /* the statement being read: where parts made for it are written */
static const char *vi_here(void) { return vi_bind_at ? vi_bind_at : vi_stmt_at ? vi_stmt_at : vi_src; }

static char *vi_fresh(const char *pre)   /* a wire name nothing else uses: m3_, a4_ ... */
{
    char *w;
    do w = vi_fmt("%s%d_", pre, ++vi_nbind); while (vi_wname_used(w));
    return w;
}

/* chunk i of x (x itself when it is not wide) */
static VX vi_chunk(const VX *x, int i)
{
    int w;
    VX r;
    if (!vi_wide(x)) return *x;
    w = x->w - 32 * i < 32 ? x->w - 32 * i : 32;
    if (x->chv) r = vi_const(x->chv[i], w, x->ty);
    else r = vi_mk(x->ch[i], x->ty, w, 2);
    r.z = x->z; r.zen = x->zen; r.zmix = x->zmix;   /* a part of a tri-state value floats with it */
    return r;
}

static int vi_is_zero(const VX *x)   /* every bit known to be 0 */
{
    int i;
    if (!vi_wide(x)) return x->cst && x->v == 0;
    if (!x->chv) return 0;
    for (i = 0; i < x->nch; i++) if (x->chv[i]) return 0;
    return 1;
}

/* the pieces of a value, lowest first: its chunks, or itself */
static void vi_flat(const VX *x, VX *out, int *n)
{
    int i;
    if (!vi_wide(x)) { out[(*n)++] = *x; return; }
    for (i = 0; i < x->nch; i++) out[(*n)++] = vi_chunk(x, i);
}

/* bits hi..lo of a value of up to 32 bits */
static VX vi_nbits(const VX *x, int hi, int lo)
{
    VX r;
    if (lo == 0 && hi == x->w - 1) return *x;
    if (x->cst) r = vi_const(x->v >> lo, hi - lo + 1, x->ty);
    else r = vi_mk(hi == lo ? vi_fmt("%s[%d]", vi_wrap(x, 2), hi) : vi_fmt("%s[%d:%d]", vi_wrap(x, 2), hi, lo), x->ty, hi - lo + 1, 2);
    r.z = x->z; r.zen = x->zen; r.zmix = x->zmix;
    return r;
}

/* the tri-state of pieces put side by side: all the same, or mixed */
static void vi_zjoin(VX *r, const VX *p, int n)
{
    int i;
    r->z = p[0].z; r->zen = p[0].zen; r->zmix = 0;
    for (i = 0; i < n; i++) {
        if (p[i].zmix) r->zmix = 1;
        if (p[i].z != p[0].z || (!p[i].zen) != (!p[0].zen) || (p[i].zen && strcmp(p[i].zen, p[0].zen))) r->zmix = 1;
    }
    if (r->zmix) { r->z = 0; r->zen = NULL; }
}

/* pieces (lowest first) side by side, as one value: up to 32 bits a {...},
 * wider as chunks */
static VX vi_join(VX *p, int n, int ty)
{
    int i, total = 0, allc = 1;
    VX r;
    for (i = 0; i < n; i++) { total += p[i].w; if (!p[i].cst) allc = 0; }
    if (total <= MAXW) {
        VBuf b = { 0, 0, 0 };
        if (n == 1) { r = p[0]; r.ty = ty; return r; }
        if (allc) {
            unsigned long v = 0;
            for (i = n - 1; i >= 0; i--) v = (v << p[i].w) | p[i].v;
            r = vi_const(v, total, ty);
            vi_zjoin(&r, p, n);
            return r;
        }
        vb_add(&b, "{", 1);
        for (i = n - 1; i >= 0; i--) vbf(&b, "%s%s", i == n - 1 ? "" : ", ", p[i].s);
        vb_add(&b, "}", 1);
        r = vi_mk(b.s, ty, total, 2);
        r.cat = 1;
        vi_zjoin(&r, p, n);
        return r;
    }
    memset(&r, 0, sizeof r);
    r.ty = ty; r.w = total; r.atom = 2;
    r.nch = (total + 31) / 32;
    r.ch = (char **)calloc((size_t)r.nch, sizeof *r.ch);
    r.chv = (unsigned long *)calloc((size_t)r.nch, sizeof *r.chv);
    {
        int c, known = 1;
        for (c = 0; c < r.nch; c++) {
            int clo = 32 * c, chi = clo + 31 < total ? clo + 31 : total - 1, off = 0, m = 0;
            VX sub[64], cv;
            for (i = 0; i < n; i++) {   /* the parts of the pieces in bits clo..chi */
                int plo = off, phi = off + p[i].w - 1;
                off += p[i].w;
                if (phi < clo || plo > chi) continue;
                sub[m++] = vi_nbits(&p[i], (phi < chi ? phi : chi) - plo, (plo > clo ? plo : clo) - plo);
            }
            cv = vi_join(sub, m, ty);
            r.ch[c] = vi_wrap(&cv, 2);
            if (cv.cst) r.chv[c] = cv.v; else known = 0;
        }
        if (!known) { free(r.chv); r.chv = NULL; }
    }
    vi_zjoin(&r, p, n);
    return r;
}

/* bits hi..lo of any value */
static VX vi_bits(const VX *x, int hi, int lo)
{
    VX p[2 * VI_MAXCH], sub[2 * VI_MAXCH];
    int n = 0, m = 0, i, off = 0;
    if (!vi_wide(x)) return vi_nbits(x, hi, lo);
    vi_flat(x, p, &n);
    for (i = 0; i < n; i++) {
        int plo = off, phi = off + p[i].w - 1;
        off += p[i].w;
        if (phi < lo || plo > hi) continue;
        sub[m++] = vi_nbits(&p[i], (phi < hi ? phi : hi) - plo, (plo > lo ? plo : lo) - plo);
    }
    return vi_join(sub, m, x->ty);
}

static VX vi_cat(VX hi, VX lo, int ty)   /* hi & lo */
{
    VX p[4 * VI_MAXCH], r;
    int n = 0;
    vi_flat(&lo, p, &n);
    vi_flat(&hi, p, &n);
    r = vi_join(p, n, ty);
    if (vi_wide(&r) && !vi_wide(&lo) && !vi_wide(&hi)) {   /* '0' & a, a(31) & a: a made wider */
        VX top = vi_nbits(&lo, lo.w - 1, lo.w - 1);
        if (vi_is_zero(&hi)) { r.xb = vi_wrap(&lo, 2); r.xbw = lo.w; r.xsgn = 0; }
        else if (hi.w == 1 && !hi.cst && !top.cst && !strcmp(hi.s, top.s)) { r.xb = vi_wrap(&lo, 2); r.xbw = lo.w; r.xsgn = 1; }
    }
    return r;
}

/* x widened to w bits with its sign (or 0s), or cut to w bits */
static VX vi_wresize(VX x, int w, int sgn)
{
    VX p[4 * VI_MAXCH], top, r;
    int n = 0, ext, xbw = 0;
    char *xb = NULL;
    if (w == x.w) return x;
    if (w < x.w) { VX r = vi_bits(&x, w - 1, 0); r.ty = x.ty; return r; }
    vi_flat(&x, p, &n);
    top = vi_bits(&x, x.w - 1, x.w - 1);
    if (!vi_wide(&x)) { xb = vi_wrap(&x, 2); xbw = x.w; }
    else if (x.xb && x.xsgn == sgn) { xb = x.xb; xbw = x.xbw; }
    for (ext = w - x.w; ext > 0; ) {
        int k = ext < 32 ? ext : 32, i;
        if (!sgn) p[n++] = vi_const(0, k, x.ty);
        else if (top.cst) p[n++] = vi_const(top.v ? vi_mask(k) : 0, k, x.ty);
        else if (k == 1) p[n++] = top;
        else {
            VBuf b = { 0, 0, 0 };
            vb_add(&b, "{", 1);
            for (i = 0; i < k; i++) vbf(&b, "%s%s", i ? ", " : "", top.s);
            vb_add(&b, "}", 1);
            p[n] = vi_mk(b.s, x.ty, k, 2);
            p[n++].cat = 1;
        }
        ext -= k;
    }
    r = vi_join(p, n, x.ty);
    if (xb) { r.xb = xb; r.xbw = xbw; r.xsgn = sgn; }
    return r;
}

/* an integer as w bits, w wider than 32 */
static VX vi_wint(long v, int w, int ty)
{
    VX p[2 * VI_MAXCH];
    int n = 0, lo;
    unsigned long long u = (unsigned long long)(long long)v;
    for (lo = 0; lo < w; lo += 32) {
        int k = w - lo < 32 ? w - lo : 32;
        unsigned long c = lo < 64 ? (unsigned long)((u >> lo) & 0xffffffffull) : (v < 0 ? 0xffffffffUL : 0);
        p[n++] = vi_const(c, k, ty);
    }
    return vi_join(p, n, ty);
}

/* a value made of bits '0' and '1', the highest first (a wide literal) */
static VX vi_wbits(const char *bits, int w, int ty)
{
    VX p[2 * VI_MAXCH];
    int n = 0, lo;
    for (lo = 0; lo < w; lo += 32) {
        int k = w - lo < 32 ? w - lo : 32, i;
        unsigned long c = 0;
        for (i = k - 1; i >= 0; i--) c = (c << 1) | (unsigned long)(bits[w - 1 - (lo + i)] == '1');
        p[n++] = vi_const(c, k, ty);
    }
    return vi_join(p, n, ty);
}

static VX vi_not(VX x);

static VX vi_wnot(VX x)
{
    VX p[2 * VI_MAXCH];
    int n = 0, i;
    if (!vi_wide(&x)) return vi_not(x);
    vi_flat(&x, p, &n);
    for (i = 0; i < n; i++) p[i] = vi_not(p[i]);
    return vi_join(p, n, x.ty);
}

/* a + b (+ 1), the same width, each 32 bits an adder, the carry into the next */
static VX vi_wadd(VX a, VX b, int cin)
{
    VX p[2 * VI_MAXCH], ac, bc;
    int n = 0, c, nch = (a.w + 31) / 32;
    char *carry = cin ? "1" : NULL;
    for (c = 0; c < nch; c++) {
        char *nm;
        ac = vi_chunk(&a, c); bc = vi_chunk(&b, c);
        if (!carry && vi_is_zero(&bc)) { p[n++] = ac; continue; }
        if (!carry && vi_is_zero(&ac)) { p[n++] = bc; continue; }
        nm = vi_fresh("a");
        vi_emitp(vi_here(), "wire %s[%d]", nm, ac.w);
        vi_emitp(vi_here(), "%s = add(%s, %s%s%s)", nm, ac.s, bc.s, carry ? ", cin = " : "", carry ? carry : "");
        p[n++] = vi_mk(nm, a.ty, ac.w, 2);
        carry = vi_fmt("%s.cout", nm);
    }
    return vi_join(p, n, a.ty);
}

/* x shifted by a constant k: what vi_shift does, for any width */
static VX vi_wshift(VX x, int left, int arith, int rot, long k)
{
    int w = x.w;
    VX r;
    if (k < 0) { left = !left; k = -k; }
    if (rot) k %= w;
    if (k == 0) return x;
    if (rot)
        r = left ? vi_cat(vi_bits(&x, w - 1 - (int)k, 0), vi_bits(&x, w - 1, w - (int)k), x.ty)
                 : vi_cat(vi_bits(&x, (int)k - 1, 0), vi_bits(&x, w - 1, (int)k), x.ty);
    else if (k >= w)
        r = vi_wresize(arith && !left ? vi_bits(&x, w - 1, w - 1) : vi_const(0, 1, x.ty), w, arith && !left);
    else if (left)
        r = vi_cat(vi_bits(&x, w - 1 - (int)k, 0), vi_wresize(vi_const(0, 1, x.ty), (int)k, 0), x.ty);
    else
        r = vi_wresize(vi_bits(&x, w - 1, (int)k), w, arith);
    r.ty = x.ty;
    return r;
}

/* a times b: the whole product, as wide as both together. Up to 32 bits
 * each, one mul part; wider (up to 64), the 32-bit chunks multiplied
 * pairwise and added up, the way it is done by hand */
static VX vi_mul_vals(VX a, VX b, int ty)
{
    int sgn = vi_signed(ty), m = a.w, n = b.w, P = m + n;
    VX r;
    if (a.cst && b.cst && P <= MAXW) {   /* known: worked out here */
        long long x = (long long)a.v, y = (long long)b.v;
        if (sgn && (a.v >> (m - 1) & 1)) x -= 1LL << m;
        if (sgn && (b.v >> (n - 1) & 1)) y -= 1LL << n;
        return vi_const((unsigned long)((unsigned long long)(x * y) & 0xffffffffull), P, ty);
    }
    {   /* each side up to 32 bits, or a value of up to 32 bits made wider ('0' & b, a(31) & a):
         * one mul part, signed or not on each side, gives the whole product */
        VX ba = a, bb = b;
        int sa = sgn, sb = sgn;
        if (vi_wide(&a) && a.xb && a.xbw <= MAXW) { ba = vi_mk(a.xb, ty, a.xbw, 2); sa = a.xsgn; }
        if (vi_wide(&b) && b.xb && b.xbw <= MAXW) { bb = vi_mk(b.xb, ty, b.xbw, 2); sb = b.xsgn; }
        if (!vi_wide(&ba) && !vi_wide(&bb)) {
            int W = ba.w > bb.w ? ba.w : bb.w;
            VX A = vi_resize(ba, W, sa), B = vi_resize(bb, W, sb), lo, hi;
            char *nm = vi_fresh("m");
            vi_emitp(vi_here(), "wire %s[%d]", nm, W);
            vi_emitp(vi_here(), "%s = mul(%s, %s%s)", nm, A.s, B.s,
                     sa && sb ? ", signed = 1" : sa ? ", sa = 1" : sb ? ", sb = 1" : "");
            lo = vi_mk(nm, ty, W, 2);
            hi = vi_mk(vi_fmt("%s.hi", nm), ty, W, 2);
            r = vi_cat(hi, lo, ty);
            r = P <= 2 * W ? vi_bits(&r, P - 1, 0) : vi_wresize(r, P, sa || sb);
            r.ty = ty;
            return r;
        }
    }
    {
        int C = (P + 31) / 32, i, j, first = 1;
        VX A = vi_wresize(a, 32 * C, sgn), B = vi_wresize(b, 32 * C, sgn), acc;
        memset(&acc, 0, sizeof acc);
        for (i = 0; i < C; i++)
            for (j = 0; i + j < C; j++) {
                VX ai = vi_chunk(&A, i), bj = vi_chunk(&B, j), p[2 * VI_MAXCH], t;
                char *nm;
                int k, np = 0;
                if (vi_is_zero(&ai) || vi_is_zero(&bj)) continue;
                nm = vi_fresh("m");
                vi_emitp(vi_here(), "wire %s[32]", nm);
                vi_emitp(vi_here(), "%s = mul(%s, %s)", nm, ai.s, bj.s);
                for (k = 0; k < C; k++)
                    p[np++] = k == i + j ? vi_mk(nm, ty, 32, 2) : k == i + j + 1 ? vi_mk(vi_fmt("%s.hi", nm), ty, 32, 2)
                                                                       : vi_const(0, 32, ty);
                t = vi_join(p, np, ty);
                acc = first ? t : vi_wadd(acc, t, 0);
                first = 0;
            }
        if (first) acc = vi_wint(0, 32 * C, ty);
        r = vi_bits(&acc, P - 1, 0);
        r.ty = ty;
        return r;
    }
}

/* an integer used where a vector of w bits is */
static VX vi_int_as(const VX *i, int w, int ty, gram_node *at)
{
    long v = i->iv;
    if (w > MAXW) {
        if (!vi_signed(ty) && v < 0) vi_fail(at, "%ld does not fit in %d bits (%s)", v, w, vi_tyname[ty]);
        return vi_wint(v, w, ty);
    }
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
    char *base = s->kind == VK_CONST || s->kind == VK_GEN ? vi_lit(s->bits, s->w) : s->wname;
    VSym cs;
    if (s->kind == VK_GEN) { cs = *s; cs.kind = VK_CONST; s = &cs; }   /* a vector generic: a constant */
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
            if (ix.w > 5) {   /* a mux of muxes */
                char **d;
                if (ix.w > 12) vi_fail(arg, "an index picked by a signal has up to 12 bits, and this has %d", ix.w);
                n = 1 << ix.w;
                d = (char **)malloc((size_t)n * sizeof *d);
                for (j = 0; j < n; j++) {
                    if (j < lo || j > hi) { d[j] = "0b0"; continue; }
                    k = s->down ? j - lo : hi - j;
                    d[j] = s->kind == VK_CONST ? vi_fmt("0b%lu", (s->bits >> k) & 1) : vi_fmt("%s[%d]", base, k);
                }
                return vi_mk(vi_muxn(d, n, ix.s, ix.w, 1), T_SL, 1, 2);
            }
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
        x = vi_gen_any(args->child, want);
        if ((to == T_SL) != (x.ty == T_SL || (x.ty == T_STR && x.w == 1 && args->child->rule == VHDL_R_chr)))
            vi_fail(args->child, "this is not a %.*s", VT(id));
        if (x.ty == T_INT) vi_fail(args->child, "an integer is not a %.*s", VT(id));
        x.ty = to;
        return x;
    }
    if (s && s->aliased && args->rule == VHDL_R_args) {   /* a bit or slice of an alias: of what it names, in its range */
        VX v = vi_gen_any(s->aliased, -1);
        VSym tmp = *s;
        if (args->next) vi_fail(args->next, "one index or slice at a time, please");
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        tmp.aliased = NULL;
        tmp.kind = v.cst ? VK_CONST : VK_SIG;
        tmp.bits = v.v;
        tmp.wname = vi_wrap(&v, 2);
        return vi_index(&tmp, id, args->child);
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
    if (s && s->wnch) {   /* a bit or slice of a signal wider than 32 bits */
        int hi, lo, one;
        long c;
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        if (args->child->rule != VHDL_R_drange && !vi_ceval(args->child, &c)) {   /* a bit picked by a signal: a mux of muxes */
            VX ix = vi_gen(args->child, -1), all = vi_wide_read(s, id);
            char **d;
            int j, nn, lo2 = vi_low(s), hi2 = vi_high(s);
            if (ix.ty != T_INTV && ix.ty != T_UNS) vi_fail(args->child, "an index is an integer: write %.*s(to_integer(...))", VT(id));
            if (ix.w > 12) vi_fail(args->child, "an index picked by a signal has up to 12 bits, and this has %d", ix.w);
            vi_bind(&ix);
            nn = 1 << ix.w;
            d = (char **)malloc((size_t)nn * sizeof *d);
            for (j = 0; j < nn; j++) {
                VX bit;
                if (j < lo2 || j > hi2) { d[j] = "0b0"; continue; }
                bit = vi_bits(&all, s->down ? j - lo2 : hi2 - j, s->down ? j - lo2 : hi2 - j);
                d[j] = vi_wrap(&bit, 2);
            }
            return vi_mk(vi_muxn(d, nn, ix.s, ix.w, 1), T_SL, 1, 2);
        }
        one = vi_bitrange(s, id, args->child, &hi, &lo);
        x = vi_wide_read(s, id);
        x = vi_bits(&x, hi, lo);
        x.ty = one ? T_SL : s->ty;
        return x;
    }
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
    if (s && !((s->kind == VK_GEN || s->kind == VK_CONST) && s->ty == T_INT)) {
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        if (s->inout && s->rname) {   /* read where all its drivers meet */
            VSym tmp = *s;
            tmp.wname = s->rname;
            return vi_index(&tmp, id, args->child);
        }
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
    if (vi_is(id, "std_logic") || vi_is(id, "std_ulogic")) {   /* a bit, as a bit */
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s() takes one bit", VT(id));
        x = vi_gen(args->child, 1);
        if (x.w != 1 || x.ty == T_INT || x.ty == T_BOOL) vi_fail(args->child, "%.*s() converts a bit, but this is %s", VT(id), vi_desc(&x));
        x.ty = T_SL;
        return x;
    }
    if (vi_is(id, "unsigned") || vi_is(id, "signed") || vi_is(id, "std_logic_vector") || vi_is(id, "std_ulogic_vector")) {
        int to = vi_is(id, "unsigned") ? T_UNS : vi_is(id, "signed") ? T_SGN : T_SLV;
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s() takes one value", VT(id));
        x = vi_gen_any(args->child, want);
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
        if (w < 1 || w > VI_MAXWIDE) vi_fail(vi_arg(args, 1), "Wire handles 1 to %d bits", VI_MAXWIDE);
        v = vi_gen(vi_arg(args, 0), -1);
        if (v.ty == T_INTV) {   /* an integer signal or variable: its bits, to that width */
            x = vi_resize(v, (int)w, 0);
            x.ty = to;
            return x;
        }
        if (v.ty == T_SGN) {   /* an integer that can be negative (signed bits): with its sign */
            x = vi_resize(v, (int)w, 1);
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
        if (w < 1 || w > VI_MAXWIDE) vi_fail(vi_arg(args, 1), "Wire handles 1 to %d bits", VI_MAXWIDE);
        x = vi_gen_any(vi_arg(args, 0), -1);
        if (x.ty != T_UNS && x.ty != T_SGN) vi_fail(vi_arg(args, 0), "resize works on unsigned or signed");
        if (x.ty == T_SGN && w < x.w && (w > MAXW || vi_wide(&x))) {   /* numeric_std keeps the sign bit */
            VX top = vi_bits(&x, x.w - 1, x.w - 1);
            if (w == 1) { top.ty = T_SGN; return top; }
            return vi_cat(top, vi_bits(&x, (int)w - 2, 0), T_SGN);
        }
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
        x = vi_gen_any(vi_arg(args, 0), want);
        if (x.ty != T_UNS && x.ty != T_SGN) vi_fail(vi_arg(args, 0), "%.*s works on unsigned or signed", VT(id));
        if (vi_wide(&x)) {
            if (!vi_ceval(vi_arg(args, 1), &k))   /* by a signal: a stage per bit of it */
                return vi_barrel(x, vi_gen(vi_arg(args, 1), -1), vi_is(id, "shift_left") || vi_is(id, "rotate_left"), x.ty == T_SGN,
                                 vi_is(id, "rotate_left") || vi_is(id, "rotate_right"), n);
            return vi_wshift(x, vi_is(id, "shift_left") || vi_is(id, "rotate_left"), x.ty == T_SGN,
                             vi_is(id, "rotate_left") || vi_is(id, "rotate_right"), k);
        }
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
        if (x.ty == T_SGN) {   /* an integer that can be negative: the signed bits */
            if (x.cst) {
                VX c;
                long v = (long)x.v;
                if (x.w < 64 && ((x.v >> (x.w - 1)) & 1)) v -= 1L << x.w;
                memset(&c, 0, sizeof c); c.ty = T_INT; c.cst = 1; c.iv = v; c.s = vi_fmt("%ld", v); c.atom = 2;
                return c;
            }
            return x;
        }
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
    int zok = vi_zok, nz = 0;
    static char zbit[] = "Z";
    vi_zok = 0;
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
        VX v;
        vi_zok = zok;
        v = vi_gen(e, 1);
        vi_zok = 0;
        if (v.ty == T_INT || v.w != 1) vi_fail(e, "each element here is one bit, like '0', '1' or a std_logic signal");
        if (v.z) v.s = zbit;
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
    for (i = 0; i < w; i++) if (bit[i] == zbit) nz++;
    if (nz) {   /* (others => 'Z'): nothing drives it */
        if (nz != w) vi_fail(n, "this aggregate has some bits 'Z' and some not; give the 'Z' bits and the others "
                                "to slices of the signal in two assignments");
        return vi_zval(w, T_STR);
    }
    if (w > MAXW) {   /* wider than a Wire wire: runs of constant bits and single bits, in chunks */
        VX *p = (VX *)calloc((size_t)w, sizeof *p);
        int np = 0;
        for (i = 0; i < w; ) {
            if (!strcmp(bit[i], "0b0") || !strcmp(bit[i], "0b1")) {
                unsigned long v = 0;
                int j = i;
                while (j < w && j - i < 32 && (!strcmp(bit[j], "0b0") || !strcmp(bit[j], "0b1"))) {
                    v |= (unsigned long)(bit[j][2] == '1') << (j - i);
                    j++;
                }
                p[np++] = vi_const(v, j - i, T_STR);
                i = j;
            } else p[np++] = vi_mk(bit[i++], T_STR, 1, 2);
        }
        return vi_join(p, np, T_STR);
    }
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
        x[m] = vi_gen_any(k, want);
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
    if (w > MAXW) {   /* chunk by chunk */
        VX p[2 * VI_MAXCH], y[64];
        int c, nch = (w + 31) / 32;
        for (c = 0; c < nch; c++) {
            VBuf cb = { 0, 0, 0 };
            for (i = 0; i < m; i++) y[i] = vi_chunk(&x[i], c);
            if (vi_is(op, "nand") || vi_is(op, "nor") || vi_is(op, "xnor")) {
                char kw[8];
                snprintf(kw, sizeof kw, "%c%c%c%c", vi_lower(op->text[0]), vi_lower(op->text[1]), vi_lower(op->text[2]),
                         op->len > 3 ? vi_lower(op->text[3]) : 0);
                p[c] = vi_mk(vi_fmt("%s(%s, %s)", kw, y[0].s, y[1].s), ty, y[0].w, 2);
                continue;
            }
            for (i = 0; i < m; i++)
                vbf(&cb, "%s%s", i ? (vi_is(op, "and") ? " & " : vi_is(op, "or") ? " | " : " ^ ") : "", vi_wrap(&y[i], 1));
            p[c] = vi_mk(cb.s, ty, y[0].w, 0);
        }
        return vi_join(p, nch, ty);
    }
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
        if (o->ty == T_SGN) {   /* with a signed (integer) value: a bit more, so it stays positive */
            int w = iv->w + 1 > o->w ? iv->w + 1 : o->w;
            *iv = vi_resize(*iv, w, 0); iv->ty = T_SGN;
            *o = vi_resize(*o, w, 1);
        } else if (vi_num(o->ty) || o->ty == T_STR) { *iv = vi_resize(*iv, o->w, 0); iv->ty = o->ty == T_STR ? T_UNS : o->ty; }
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

/* a comparison of values wider than 32 bits: 32 bits at a time, the highest first */
static VX vi_wcompare(VX a, VX b, const char *o)
{
    int nch = (a.w + 31) / 32, c, eq = !strcmp(o, "=") || !strcmp(o, "/=");
    int lt = !strcmp(o, "<") || !strcmp(o, ">="), neg = !strcmp(o, "/=") || !strcmp(o, ">=") || !strcmp(o, "<=");
    VBuf t = { 0, 0, 0 };
    VX r;
    if (!eq && vi_signed(a.ty)) {   /* signed: flip the sign bits and compare as unsigned */
        VX *p[2];
        int i;
        p[0] = &a; p[1] = &b;
        for (i = 0; i < 2; i++) {
            VX top = vi_bits(p[i], p[i]->w - 1, p[i]->w - 1), rest = vi_bits(p[i], p[i]->w - 2, 0);
            *p[i] = vi_cat(vi_not(top), rest, p[i]->ty);
        }
    }
    {   /* chunk by chunk: = all of them; < the top one, or equal there and < below. Known chunks are worked out here */
        VX res = vi_const(eq ? 1 : 0, 1, T_BOOL);
        for (c = 0; c < nch; c++) {
            VX x = vi_chunk(&a, c), y = vi_chunk(&b, c), e1, l1;
            if (x.cst && y.cst) {
                e1 = vi_const(x.v == y.v, 1, T_BOOL);
                l1 = vi_const(lt ? x.v < y.v : x.v > y.v, 1, T_BOOL);
            } else {
                e1 = vi_mk(vi_fmt("compare(%s, %s).eq", x.s, y.s), T_BOOL, 1, 2);
                l1 = vi_mk(vi_fmt("compare(%s, %s).%s", x.s, y.s, lt ? "lt" : "gt"), T_BOOL, 1, 2);
            }
            if (eq) {   /* res & e1 */
                if (res.cst) res = res.v ? e1 : res;
                else if (!e1.cst) res = vi_mk(vi_fmt("%s & %s", vi_wrap(&res, 1), vi_wrap(&e1, 1)), T_BOOL, 1, 0);
                else if (!e1.v) res = e1;
            } else {   /* l1 | e1 & res */
                VX t2;
                if (e1.cst) t2 = e1.v ? res : e1;
                else if (res.cst) t2 = res.v ? e1 : res;
                else t2 = vi_mk(vi_fmt("%s & %s", vi_wrap(&e1, 1), vi_wrap(&res, 1)), T_BOOL, 1, 0);
                if (l1.cst) res = l1.v ? l1 : t2;
                else if (t2.cst) res = t2.v ? t2 : l1;
                else res = vi_mk(vi_fmt("%s | %s", l1.s, vi_wrap(&t2, 0)), T_BOOL, 1, 0);
            }
        }
        r = res.cst ? res : vi_mk(vi_fmt("(%s)", res.s), T_BOOL, 1, 2);
    }
    if (neg) r = vi_not(r);
    r.ty = T_BOOL;
    (void)t;
    return r;
}

/* ---- truth tables as gates -------------------------------------------------
 * A table of constants (with value select ... "0110" when "001", ...) or a
 * comparison with a constant (roll = 6, r >= 1) depends only on a few bits.
 * Logisim draws those as gates, the way its Combinational Analysis makes them
 * from a truth table: for each output bit an OR of ANDs of the input bits or
 * their inverses, as few and as small as they can be (Quine-McCluskey). So
 * does this, instead of a mux fed by constants or a comparator and a
 * constant. The bits are read as x[i] of a signal; each inverse is one NOT
 * gate, shared by every AND that uses it. */

#define VI_SOP_BITS 6            /* input bits a table is made of gates for, at most */
#define VI_SOP_COST 96           /* AND inputs in all the terms of one table, at most */
#define VI_SOP_IMP 729           /* implicants of 6 bits, at most (3 to the 6th) */

static int vi_ones(unsigned x) { int n = 0; for (; x; x &= x - 1) n++; return n; }

/* x as something its bits can be read from with x[i]: a signal's name */
static int vi_bitable(const VX *x)
{
    const char *p = x->s;
    if (!p || x->cst || vi_wide(x) || x->z || x->zen) return 0;
#define VI_ID0(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || (c) == '_')
    if (!VI_ID0(*p)) return 0;
    for (p++; *p; p++) if (!VI_ID0(*p) && !(*p >= '0' && *p <= '9')) return 0;
#undef VI_ID0
    return 1;
}

/* the ORs of ANDs that are 1 exactly where on[] is (on has 1 << nb entries),
 * over the bits of x; literals counts the AND inputs. A 0 or 1 everywhere is
 * a constant. */
static VX vi_sop(const VX *x, int nb, const unsigned char *on, int *literals)
{
    unsigned v[VI_SOP_IMP], m[VI_SOP_IMP], pv[VI_SOP_IMP], pm[VI_SOP_IMP], nv[VI_SOP_IMP], nm[VI_SOP_IMP];
    unsigned char used[VI_SOP_IMP], cov[1 << VI_SOP_BITS], pick[VI_SOP_IMP];
    int n = 0, np = 0, i, j, k, full = 1 << nb, ones = 0, nterm = 0;
    VBuf b = { 0, 0, 0 };
    for (i = 0; i < full; i++) if (on[i]) ones++;
    if (!ones || ones == full) return vi_const(ones ? 1 : 0, 1, T_SL);
    /* the minterms, then merge pairs that differ in one bit until none do;
     * what is never merged is a prime implicant */
    for (i = 0; i < full; i++) if (on[i]) { v[n] = (unsigned)i; m[n] = 0; n++; }
    while (n) {
        int nn = 0;
        memset(used, 0, sizeof used);
        for (i = 0; i < n; i++)
            for (j = i + 1; j < n; j++) {
                unsigned d = v[i] ^ v[j];
                if (m[i] != m[j] || !d || (d & (d - 1))) continue;
                used[i] = used[j] = 1;
                for (k = 0; k < nn; k++) if (nv[k] == (v[i] & ~d) && nm[k] == (m[i] | d)) break;
                if (k == nn && nn < (int)(sizeof nv / sizeof nv[0])) { nv[nn] = v[i] & ~d; nm[nn] = m[i] | d; nn++; }
            }
        for (i = 0; i < n; i++)
            if (!used[i] && np < (int)(sizeof pv / sizeof pv[0])) {
                for (k = 0; k < np; k++) if (pv[k] == v[i] && pm[k] == m[i]) break;
                if (k == np) { pv[np] = v[i]; pm[np] = m[i]; np++; }
            }
        memcpy(v, nv, (size_t)nn * sizeof *v);
        memcpy(m, nm, (size_t)nn * sizeof *m);
        n = nn;
    }
    /* cover the minterms: first the primes that alone cover one, then the
     * prime covering most of what is left (fewest inputs on a tie) */
    memset(cov, 0, sizeof cov);
    memset(pick, 0, sizeof pick);
    for (i = 0; i < full; i++) {
        int only = -1, cnt = 0;
        if (!on[i]) continue;
        for (k = 0; k < np; k++) if (((unsigned)i & ~pm[k]) == pv[k]) { only = k; cnt++; }
        if (cnt == 1) pick[only] = 1;
    }
    for (;;) {
        int best = -1, bestn = 0;
        for (k = 0; k < np; k++) if (pick[k])
            for (i = 0; i < full; i++) if (on[i] && ((unsigned)i & ~pm[k]) == pv[k]) cov[i] = 1;
        for (k = 0; k < np; k++) {
            int c = 0;
            if (pick[k]) continue;
            for (i = 0; i < full; i++) if (on[i] && !cov[i] && ((unsigned)i & ~pm[k]) == pv[k]) c++;
            if (c > bestn || (c == bestn && c && vi_ones(pm[k]) > vi_ones(pm[best]))) { best = k; bestn = c; }
        }
        if (best < 0) break;
        pick[best] = 1;
    }
    /* write it: terms in order of their value, bits from the top */
    *literals = 0;
    for (i = 0; i < full; i++)
        for (k = 0; k < np; k++) {
            int lits = 0;
            if (!pick[k] || pv[k] != (unsigned)i) continue;
            pick[k] = 0;
            vbf(&b, "%s", nterm++ ? " | " : "");
            for (j = nb - 1; j >= 0; j--) {
                if (pm[k] >> j & 1) continue;
                vbf(&b, "%s%s%s", lits++ ? " & " : "", pv[k] >> j & 1 ? "" : "~",
                    x->w == 1 ? x->s : vi_fmt("%s[%d]", x->s, j));
            }
            *literals += lits;
        }
    if (nterm == 1 && !strchr(b.s, '&')) return vi_mk(b.s, T_SL, 1, b.s[0] == '~' ? 1 : 2);
    return vi_mk(vi_fmt("(%s)", b.s), T_SL, 1, 2);
}

/* x (o) c, c a constant, as gates over the bits of x, when x is small enough */
static int vi_cmp_gates(VX *r, const VX *x, unsigned long c, const char *o, int flipped)
{
    unsigned char on[1 << VI_SOP_BITS];
    int i, full, lits, sgn = vi_signed(x->ty);
    long cv = (long)c;
    if (x->w > VI_SOP_BITS || !vi_bitable(x)) return 0;
    full = 1 << x->w;
    if (sgn && cv >> (x->w - 1) & 1) cv -= 1L << x->w;
    for (i = 0; i < full; i++) {
        long a = i, p, q;
        if (sgn && (i >> (x->w - 1) & 1)) a -= 1L << x->w;
        p = flipped ? cv : a; q = flipped ? a : cv;   /* c (o) x when the constant came first */
        on[i] = !strcmp(o, "=") ? p == q : !strcmp(o, "/=") ? p != q : !strcmp(o, "<") ? p < q
              : !strcmp(o, "<=") ? p <= q : !strcmp(o, ">") ? p > q : p >= q;
    }
    *r = vi_sop(x, x->w, on, &lits);
    r->ty = T_BOOL;
    return 1;
}

/* a table of constants, tab[value of sel] (1 << sel.w of them), as gates:
 * one OR of ANDs per bit of the result; 0 when it is too big for that */
static int vi_table_gates(VX *r, const VX *sel, const VX *tab, int n, int w, int ty)
{
    unsigned char on[1 << VI_SOP_BITS];
    VBuf b = { 0, 0, 0 };
    VX bit[MAXW];
    int i, j, cost = 0, lits;
    if (sel->w > VI_SOP_BITS || w > MAXW || !vi_bitable(sel)) return 0;
    for (i = 0; i < n; i++) if (!tab[i].cst || vi_wide(&tab[i]) || tab[i].z) return 0;
    for (j = 0; j < w; j++) {
        for (i = 0; i < n; i++) on[i] = (unsigned char)(tab[i].v >> j & 1);
        bit[j] = vi_sop(sel, sel->w, on, &lits);
        cost += lits;
    }
    if (cost > VI_SOP_COST) return 0;
    if (w == 1) { *r = bit[0]; r->ty = ty; return 1; }
    vb_add(&b, "{", 1);
    for (j = w - 1; j >= 0; j--) vbf(&b, "%s%s", j < w - 1 ? ", " : "", bit[j].s);
    vb_add(&b, "}", 1);
    *r = vi_mk(b.s, ty, w, 2);
    return 1;
}

static VX vi_relation(gram_node *n)
{
    gram_node *ln = n->child, *op = ln->next, *rn = op->next;
    VX a, b, r;
    char o[3] = { 0, 0, 0 };
    int sgn;
    if (ln->rule == VHDL_R_aggregate) {   /* (others => '0') = x: its width comes from the other side */
        b = vi_gen_any(rn, -1);
        a = vi_gen_any(ln, b.ty == T_INT ? -1 : b.w);
    } else {
        a = vi_gen_any(ln, -1);
        b = vi_gen_any(rn, a.ty == T_INT ? -1 : a.w);
    }
    memcpy(o, op->text, op->len < 2 ? op->len : 2);
    vi_pair(&a, &b, ln, rn, o, 0);
    if (vi_wide(&a)) return vi_wcompare(a, b, o);
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
    if (a.cst != b.cst) {   /* with a constant: gates over the few bits of the other side */
        const VX *c = a.cst ? &a : &b, *x = a.cst ? &b : &a;
        if (vi_cmp_gates(&r, x, c->v, o, a.cst)) return r;
    }
    sgn =vi_signed(a.ty) && strcmp(o, "=") && strcmp(o, "/=");
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
    VX acc = vi_gen_any(k, k->next && vi_is(k->next, "&") ? -1 : want);
    unsigned long lastv = 0;   /* the constant at the end of acc's {...}, if there is one */
    int lastw = 0;
    for (k = k->next; k; k = k->next->next) {
        gram_node *rn = k->next;
        if (vi_is(k, "&")) {
            VX b = vi_gen_any(rn, -1);
            vi_need_value(&acc, n->child);
            vi_need_value(&b, rn);
            if (acc.ty == T_BOOL || b.ty == T_BOOL) vi_fail(k, "& joins bits; a condition is not a bit");
            if (acc.w + b.w > VI_MAXWIDE) vi_fail(k, "Wire handles up to %d bits, and this is %d", VI_MAXWIDE, acc.w + b.w);
            if (acc.w + b.w > MAXW) {   /* wider than a Wire wire: in chunks */
                int ty = vi_vec(acc.ty) && acc.ty != T_STR ? acc.ty : vi_vec(b.ty) && b.ty != T_STR ? b.ty : T_STR;
                acc = vi_cat(acc, b, ty);
                lastw = 0;
                continue;
            }
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
            VX b = vi_gen_any(rn, acc.ty == T_INT ? -1 : acc.w);
            vi_pair(&acc, &b, n->child, rn, sub ? "-" : "+", 1);
            if (acc.ty == T_INT) { acc.iv = sub ? acc.iv - b.iv : acc.iv + b.iv; acc.s = vi_fmt("%ld", acc.iv); continue; }
            if (vi_wide(&acc)) { int ty = acc.ty; acc = vi_wadd(acc, sub ? vi_wnot(b) : b, sub); acc.ty = ty; continue; }
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
/* live: 0 after exit, until the loop ends; ret: 0 once a return has happened
 * (no s: 1), so a return inside a loop stops the rest of the function too */
typedef struct { VSlot *s; int n, cap; VX live, ret; } VEnv;
static VEnv *vi_env;             /* inside a process */
static int vi_clocked;           /* the process is clocked: a signal it does not set keeps its value */

static VX vi_slot_init(int si, unsigned long *mask)
{
    VSym *s = &vi_syms[si];
    VX x;
    if (vi_clocked && s->kind != VK_VAR) {   /* a register keeps its value */
        x = vi_mk(s->zd ? s->zd : s->wname, s->ty, s->w, 2);
        x.tid = s->ut;
        if (s->zd) x.zen = s->ze;   /* and whether it is driven */
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

/* a signal or variable wider than 32 bits: its wires (or values so far), side by side */
static VX vi_wide_read(VSym *s, gram_node *at)
{
    VX p[2 * VI_MAXCH];
    int c;
    for (c = 0; c < s->wnch; c++) {
        VSym *ch = &vi_syms[s->wch0 + c];
        p[c] = ch->kind == VK_VAR ? vi_var_read(ch, at) : vi_mk(ch->wname, ch->ty, ch->w, 2);
        p[c].ty = s->ty;
    }
    return vi_join(p, s->wnch, s->ty);
}

/* arr(3), arr(i), arr(i)(7 downto 4): an element; picked by a signal, a mux (or a rom, for a constant) */
static void vi_bind(VX *x);
static char *vi_muxn(char **d, int n, const char *sel, int selw, int w);

/* element i (counted from 0, row by row) of array s, as a value */
static VX vi_elem_val(VSym *s, int i, gram_node *at)
{
    VType *t = &vi_types[s->ut - 1];
    VSym *el;
    VX e;
    if (s->kind == VK_CONST) e = vi_const(s->abits[i], t->elem.w, t->elem.ty);
    else {
        el = &vi_syms[s->elem0 + i];
        e = el->kind == VK_VAR ? vi_var_read(el, at) : vi_mk(el->wname, el->ty, el->w, 2);
    }
    e.tid = t->elem.ut;
    return e;
}

/* the index nodes of arr(i), arr(i, j), arr(i)(j); *rest: what follows them (a bit or slice of the element) */
static int vi_array_idx(VSym *s, gram_node *id, gram_node *args, gram_node **ix, gram_node **rest)
{
    VType *t = &vi_types[s->ut - 1];
    if (args->rule != VHDL_R_args) vi_fail(args, "pick one element of %.*s, like %.*s(0)", VT(id), VT(id));
    ix[0] = args->child;
    *rest = args->next;
    if (t->two == 1) {
        if (vi_nargs(args) != 2) vi_fail(args, "%.*s has two indexes: pick an element like %.*s(0, 0)", VT(id), VT(id));
        ix[1] = args->child->next;
    } else {
        if (vi_nargs(args) != 1) vi_fail(args, "pick one element of %.*s, like %.*s(0)", VT(id), VT(id));
        if (t->two == 2) {
            if (!args->next || args->next->rule != VHDL_R_args || vi_nargs(args->next) != 1)
                vi_fail(args, "%.*s is an array of arrays: pick an element like %.*s(0)(0)", VT(id), VT(id));
            ix[1] = args->next->child;
            *rest = args->next->next;
        }
    }
    if (ix[0]->rule == VHDL_R_drange || (t->two && ix[1]->rule == VHDL_R_drange))
        vi_fail(args, "pick one element of %.*s, not a slice of it", VT(id));
    return t->two ? 2 : 1;
}

/* arr(3), arr(i), arr(i)(7 downto 4), m(i, j), m(i)(j): an element; picked by a signal, a mux (or a rom, for a constant) */
static VX vi_array_read(VSym *s, gram_node *id, gram_node *args, int want)
{
    VType *t = &vi_types[s->ut - 1];
    VX e;
    gram_node *ixn[2] = { NULL, NULL }, *rest;
    long k[2];
    int nd, isc[2], d, cols = t->two ? t->bhi - t->blo + 1 : 1;
    VX ix[2];
    (void)want;
    nd = vi_array_idx(s, id, args, ixn, &rest);
    for (d = 0; d < nd; d++) {
        int lo = d ? t->blo : t->alo, hi = d ? t->bhi : t->ahi;
        isc[d] = vi_ceval(ixn[d], &k[d]);
        if (isc[d]) {
            if (k[d] < lo || k[d] > hi) vi_fail(ixn[d], "%ld is outside %.*s(%d to %d)", k[d], VT(id), lo, hi);
        } else {
            ix[d] = vi_gen(ixn[d], -1);
            if (ix[d].ty != T_INTV && ix[d].ty != T_UNS)
                vi_fail(ixn[d], "an index is an integer: write %.*s(to_integer(...))", VT(id));
            if (ix[d].w > 12) vi_fail(ixn[d], "an index picked by a signal has up to 12 bits, and this has %d", ix[d].w);
            vi_bind(&ix[d]);
        }
    }
    if (nd == 1 && isc[0]) e = vi_elem_val(s, (int)(k[0] - t->alo), id);
    else if (nd == 2 && isc[0] && isc[1]) e = vi_elem_val(s, (int)((k[0] - t->alo) * cols + (k[1] - t->blo)), id);
    else if (nd == 1 && s->ramproc) {   /* a ram: read at the address it is written at */
        e = vi_mk(vi_ram_word(s), t->elem.ty, t->elem.w, 2);
        if (s->nlanes > 1) e.cat = 1;
    } else if (nd == 1 && s->kind == VK_CONST) {   /* a constant array read with a signal is a rom */
        VBuf b = { 0, 0, 0 };
        int j, n;
        if (ix[0].w > 16) vi_fail(ixn[0], "a rom has up to 16 address bits, and this index has %d", ix[0].w);
        if (t->elem.w > 32) vi_fail(ixn[0], "a rom word has up to 32 bits");
        n = 1 << ix[0].w;
        vbf(&b, "rom(%s, bits = %d, data = \"", ix[0].s, t->elem.w);
        for (j = 0; j < n; j++) vbf(&b, "%s%lX", j ? " " : "", j >= t->alo && j <= t->ahi ? s->abits[j - t->alo] : 0UL);
        vb_add(&b, "\")", 2);
        e = vi_mk(b.s, t->elem.ty, t->elem.w, 2);
    } else {   /* a mux per index picked by a signal (a mux of muxes past 32) */
        int nr = isc[0] ? 1 : 1 << ix[0].w, nc = nd == 2 && !isc[1] ? 1 << ix[1].w : 1, r, c;
        char **rows = (char **)malloc((size_t)nr * sizeof *rows), **cs = (char **)malloc((size_t)nc * sizeof *cs);
        char *z = vi_lit(0, t->elem.w);
        for (r = 0; r < nr; r++) {
            long rv = isc[0] ? k[0] : r;
            if (rv < t->alo || rv > t->ahi) { rows[r] = z; continue; }
            if (nd == 1) { VX v = vi_elem_val(s, (int)(rv - t->alo), id); rows[r] = vi_wrap(&v, 2); continue; }
            for (c = 0; c < nc; c++) {
                long cv = isc[1] ? k[1] : c;
                VX v;
                if (cv < t->blo || cv > t->bhi) { cs[c] = z; continue; }
                v = vi_elem_val(s, (int)((rv - t->alo) * cols + (cv - t->blo)), id);
                cs[c] = vi_wrap(&v, 2);
            }
            rows[r] = isc[1] ? cs[0] : vi_muxn(cs, nc, ix[1].s, ix[1].w, t->elem.w);
        }
        e = vi_mk(isc[0] ? rows[0] : vi_muxn(rows, nr, ix[0].s, ix[0].w, t->elem.w), t->elem.ty, t->elem.w, 2);
    }
    e.tid = t->elem.ut;
    if (rest) {   /* then a bit or slice of the element */
        VSym tmp = t->elem;
        if (rest->rule != VHDL_R_args || vi_nargs(rest) != 1 || rest->next)
            vi_fail(rest, "one index or slice at a time, please");
        tmp.kind = e.cst ? VK_CONST : VK_SIG;
        tmp.bits = e.v;
        tmp.wname = vi_wrap(&e, 2);
        return vi_index(&tmp, id, rest->child);
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
    if (amount.w > 5 || vi_wide(&x)) {   /* a stage per bit of the amount: by 1, 2, 4 ... when that bit is 1 */
        int k;
        vi_bind(&amount);
        for (k = 0; k < amount.w; k++) {
            long by = k < 30 ? 1L << k : 1L << 30;
            VX bit = amount.w == 1 ? amount : vi_mk(vi_fmt("%s[%d]", vi_wrap(&amount, 2), k), T_SL, 1, 2), sh;
            if (rot) by %= x.w;
            if (!by && rot) continue;
            sh = vi_wide(&x) ? vi_wshift(x, left, arith, rot, by) : vi_shift(x, left, arith, rot, by < x.w ? by : x.w, at);
            vi_bind(&x);
            vi_bind(&sh);
            x = vi_mux2(x, sh, bit);
        }
        return x;
    }
    n = 1 << amount.w;
    vb_add(&b, "mux(", 4);
    for (j = 0; j < n; j++) vbf(&b, "%s, ", vi_shift(x, left, arith, rot, j, at).s);
    vbf(&b, "sel = %s)", amount.s);
    return vi_mk(b.s, x.ty, x.w, 2);
}

static VX vi_term(gram_node *n);

/* the value of an expression, which may be wider than 32 bits */
static VX vi_gen_any(gram_node *n, int want)
{
    VX x;
    int zok = vi_zok;
    vi_zok = 0;
    memset(&x, 0, sizeof x);
    switch (n->rule) {
    case VHDL_R_chr: {
        int b = vi_bitchar(n, n->text[1]);
        if (b < 0) {
            if (!zok) vi_no_z(n);
            return vi_zval(1, T_SL);
        }
        return vi_const((unsigned long)b, 1, T_SL);
    }
    case VHDL_R_str:
        vi_zok = zok;
        x = vi_strlit(n);
        vi_zok = 0;
        return x;
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
            if (vi_find_func(n, 0)) return vi_func_call(vi_find_func(n, 0), n, NULL);   /* a function with no arguments */
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
        if (s->aliased) {   /* alias x is ...: what it names */
            x = vi_gen_any(s->aliased, want);
            if (vi_vec(s->ty) && vi_vec(x.ty) && x.ty != T_STR) x.ty = s->ty;
            return x;
        }
        if (s->ty == T_ARR) vi_fail(n, "%.*s is an array; pick an element, like %.*s(0)", VT(n), VT(n));
        if (s->ty == T_REC) vi_fail(n, "%.*s is a record; use its fields, like %.*s.%.*s", VT(n), VT(n),
                                    VT(vi_types[s->ut - 1].fid[0]));
        if (s->wnch) return vi_wide_read(s, n);
        if (s->kind == VK_VAR) return vi_var_read(s, n);
        if (s->ty == T_INT) { x.ty = T_INT; x.cst = 1; x.iv = s->ival; x.s = vi_fmt("%ld", s->ival); x.atom = 2; return x; }
        if (s->kind == VK_CONST || s->kind == VK_GEN) { x = vi_const(s->bits, s->w, s->ty); x.tid = s->ut; return x; }
        x = vi_mk(s->inout && s->rname ? s->rname : s->wname, s->ty, s->w, 2);
        x.tid = s->ut;
        return x;
    }
    case VHDL_R_name: {
        gram_node *one = vi_unfield(n->child);
        if (one) {   /* r.f, r.f(3): the field's own symbol */
            gram_node nm;
            if (!vi_find(one)) {
                VSym *r = vi_find(n->child);
                if (r && r->ty == T_REC) vi_fail(n->child->next, "%.*s has no field %.*s", VT(n->child), VT(n->child->next->child));
                vi_fail(n->child, "%.*s is not a record", VT(n->child));
            }
            if (!one->next) return vi_gen_any(one, want);
            nm = *n;
            nm.child = one;
            vi_zok = zok;
            return vi_gen_any(&nm, want);
        }
        return vi_call(n, want);
    }
    case VHDL_R_aggregate:
        vi_zok = zok;
        x = vi_aggregate(n, want);
        vi_zok = 0;
        return x;
    case VHDL_R_notx:
        x = vi_gen_any(n->child, want);
        vi_need_value(&x, n->child);
        return vi_wnot(x);
    case VHDL_R_neg:
        x = vi_gen_any(n->child, want);
        if (x.ty == T_INT) { x.iv = -x.iv; x.s = vi_fmt("%ld", x.iv); return x; }
        if (!vi_signed(x.ty)) vi_fail(n, "only signed values can be negated, and this is %s", vi_tyname[x.ty]);
        if (vi_wide(&x)) return vi_wadd(vi_wnot(x), vi_wint(0, x.w, x.ty), 1);
        if (x.cst) return vi_const(~x.v + 1, x.w, x.ty);
        return vi_mk(vi_fmt("add(%s, %s, cin = 1)", vi_not(x).s, vi_lit(0, x.w)), x.ty, x.w, 2);
    case VHDL_R_absx:
        x = vi_gen_any(n->child, want);
        if (x.ty == T_INT) { x.iv = x.iv < 0 ? -x.iv : x.iv; x.s = vi_fmt("%ld", x.iv); return x; }
        if (!vi_num(x.ty)) vi_fail(n->child, "abs works on numbers, but this is %s", vi_desc(&x));
        return vi_absval(x);
    case VHDL_R_expr:
        return vi_logic(n, want);
    case VHDL_R_relation:
        return vi_relation(n);
    case VHDL_R_shift: {
        gram_node *op = n->child->next;
        long k;
        x = vi_gen_any(n->child, want);
        if (!vi_vec(x.ty) || x.ty == T_STR) vi_fail(n->child, "%.*s shifts a vector", VT(op));
        if (vi_wide(&x)) {
            if (!vi_ceval(op->next, &k))   /* by a signal: a stage per bit of it */
                return vi_barrel(x, vi_gen(op->next, -1), vi_is(op, "sll") || vi_is(op, "sla") || vi_is(op, "rol"),
                                 vi_is(op, "sra") || vi_is(op, "sla"), vi_is(op, "rol") || vi_is(op, "ror"), n);
            return vi_wshift(x, vi_is(op, "sll") || vi_is(op, "sla") || vi_is(op, "rol"), vi_is(op, "sra") || vi_is(op, "sla"),
                             vi_is(op, "rol") || vi_is(op, "ror"), k);
        }
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
        return vi_term(n);
    default:
        vi_fail(n, "this kind of expression is not supported yet");
    }
    return x;
}

/* an expression that must fit in a Wire wire (32 bits) */
static VX vi_gen(gram_node *n, int want)
{
    VX x = vi_gen_any(n, want);
    if (vi_wide(&x))
        vi_fail(n, "this value is %d bits; values wider than 32 bits can be kept in signals and variables, sliced, joined "
                   "with &, resized, compared and used in logic, + - and *, but not here yet", x.w);
    return x;
}

/* a * b * ...: multipliers (/, mod and rem only on constant integers) */
/* the bits an integer constant needs (with a sign bit when negative or sgn) */
static int vi_ibits(long v, int sgn)
{
    int w = 1;
    if (v < 0) { while (w < 32 && -(1L << (w - 1)) > v) w++; return w; }
    while (w < 31 && (1L << w) <= v) w++;
    return sgn ? w + 1 : w;
}

/* two operands of * / mod rem as numbers of one kind: unsigned, signed or
 * integer (T_INTV, or T_SGN when it can be negative); *ity is what the result is */
static void vi_arith2(VX *a, VX *b, gram_node *an, gram_node *bn, const char *op, int *ty)
{
    int sgn;
    if (a->ty == T_INTV || b->ty == T_INTV || (a->ty == T_INT && b->ty != T_INT && !vi_vec(b->ty)) || (b->ty == T_INT && !vi_vec(a->ty))) {
        /* integers: unsigned bits, signed if either can be negative */
        sgn = (a->ty == T_SGN) || (b->ty == T_SGN) || (a->ty == T_INT && a->iv < 0) || (b->ty == T_INT && b->iv < 0);
        if (a->ty == T_INT) *a = vi_int_as(a, vi_ibits(a->iv, sgn), sgn ? T_SGN : T_INTV, an);
        if (b->ty == T_INT) *b = vi_int_as(b, vi_ibits(b->iv, sgn), sgn ? T_SGN : T_INTV, bn);
        if (sgn) {
            if (a->ty == T_INTV) { *a = vi_resize(*a, a->w + 1, 0); }
            if (b->ty == T_INTV) { *b = vi_resize(*b, b->w + 1, 0); }
            a->ty = b->ty = T_SGN;
        }
        if (!vi_num(a->ty) || !vi_num(b->ty)) vi_fail(an, "%s needs numbers: integers, unsigned or signed", op);
        if (a->ty != b->ty) vi_fail(bn, "%s needs both sides of the same kind (both integers, or convert with to_integer())", op);
        *ty = sgn ? T_SGN : T_INTV;
        return;
    }
    if (a->ty == T_INT || b->ty == T_INT) {   /* unsigned * natural: the integer gets the other's width */
        VX *i = a->ty == T_INT ? a : b, *v = i == a ? b : a;
        if (!vi_num(v->ty)) vi_fail(i == a ? bn : an, "%s with an integer needs unsigned or signed, but this is %s", op, vi_desc(v));
        *i = vi_int_as(i, v->w, v->ty, i == a ? an : bn);
    }
    if (a->ty == T_SL || a->ty == T_BOOL || b->ty == T_SL || b->ty == T_BOOL || a->ty == T_ENUM || b->ty == T_ENUM)
        vi_fail(an, "%s needs unsigned or signed values", op);
    *ty = a->ty != T_STR ? a->ty : b->ty;
    if (a->ty != T_STR && b->ty != T_STR && a->ty != b->ty)
        vi_fail(bn, "%s needs both sides of the same type: this is %s, the other side %s (convert one with unsigned() or signed())",
                op, vi_tyname[b->ty], vi_tyname[a->ty]);
    if (*ty == T_STR) *ty = T_UNS;
    if (!vi_num(*ty))
        vi_fail(an, "%s needs unsigned or signed values (use ieee.numeric_std and write unsigned(...)), but this is %s", op,
                vi_tyname[*ty]);
    a->ty = b->ty = *ty;
}

/* |x| of a signed value: -x when it is negative (as wide as x) */
static VX vi_absval(VX x)
{
    VX neg, top;
    if (!vi_signed(x.ty)) return x;
    vi_bind(&x);
    neg = vi_wide(&x) ? vi_wadd(vi_wnot(x), vi_wint(0, x.w, x.ty), 1)
                      : vi_mk(vi_fmt("add(%s, %s, cin = 1)", vi_not(x).s, vi_lit(0, x.w)), x.ty, x.w, 2);
    top = vi_bits(&x, x.w - 1, x.w - 1);
    neg.ty = x.ty;
    return vi_mux2(x, neg, top);
}

/* a / b and a rem b of unsigned values (up to 32 bits each), as restoring
 * division: a step per bit of a, each a subtractor */
static void vi_udiv(VX a, VX b, VX *q, VX *r)
{
    int i, wb = b.w;
    VX rem = vi_const(0, wb, T_UNS), bx = vi_resize(b, wb + 1, 0);
    VBuf qb = { 0, 0, 0 };
    char **qbits = (char **)malloc((size_t)a.w * sizeof *qbits);
    vi_bind(&a);
    vi_bind(&bx);
    for (i = a.w - 1; i >= 0; i--) {
        VX ai = vi_bits(&a, i, i), ext = vi_cat(rem, ai, T_UNS), t;
        char *nm = vi_fresh("d");
        vi_emitp(vi_here(), "wire %s[%d]", nm, wb + 1);
        vi_emitp(vi_here(), "%s = add(%s, %s, cin = 1)", nm, vi_wrap(&ext, 2), vi_not(bx).s);   /* ext - b; its carry: ext >= b */
        qbits[i] = vi_fmt("%s.cout", nm);
        t = vi_mux2(ext, vi_mk(nm, T_UNS, wb + 1, 2), vi_mk(qbits[i], T_SL, 1, 2));
        rem = vi_bits(&t, wb - 1, 0);
        vi_bind(&rem);
    }
    vb_add(&qb, "{", 1);
    for (i = a.w - 1; i >= 0; i--) vbf(&qb, "%s%s", i == a.w - 1 ? "" : ", ", qbits[i]);
    vb_add(&qb, "}", 1);
    *q = a.w == 1 ? vi_mk(qbits[0], T_UNS, 1, 2) : vi_mk(qb.s, T_UNS, a.w, 2);
    *r = rem;
}

/* a op b for / mod rem: numeric_std's widths (the quotient as a, the remainder as b) */
static VX vi_divide(VX a, VX b, const char *op, int ty, gram_node *at)
{
    int sgn = vi_signed(ty), isdiv = !strcmp(op, "/"), ismod = !strcmp(op, "mod");
    VX q, r, res;
    if (a.w > 32 || b.w > 32) vi_fail(at, "Wire divides values of up to 32 bits");
    if (b.cst && !sgn && b.v && !(b.v & (b.v - 1))) {   /* by a power of 2: a shift, or the low bits */
        int k = 0;
        while ((1UL << k) < b.v) k++;
        if (isdiv) res = k ? vi_shift(a, 0, 0, 0, k, at) : a;
        else res = vi_resize(k ? vi_bits(&a, k - 1 < a.w ? k - 1 : a.w - 1, 0) : vi_const(0, 1, ty), b.w, 0);
        res.ty = ty;
        return res;
    }
    if (!sgn) {
        vi_udiv(a, b, &q, &r);
        res = isdiv ? q : r;
        res.ty = ty;
        return res;
    }
    {   /* signed: the sizes divided, then the signs; rem has a's sign, mod b's */
        VX sa = vi_bits(&a, a.w - 1, a.w - 1), sb = vi_bits(&b, b.w - 1, b.w - 1), ua = vi_absval(a), ub = vi_absval(b);
        VX nq, nr, qs, rs, diff, zero;
        ua = vi_resize(ua, a.w + 1, 0); ub = vi_resize(ub, b.w + 1, 0);   /* -2^(n-1) has no positive in n bits */
        ua.ty = ub.ty = T_UNS;
        vi_bind(&sa); vi_bind(&sb);
        vi_udiv(ua, ub, &q, &r);
        q = vi_bits(&q, a.w - 1, 0); r = vi_bits(&r, b.w - 1, 0);
        q.ty = r.ty = T_SGN;
        vi_bind(&q); vi_bind(&r);
        nq = vi_mk(vi_fmt("add(%s, %s, cin = 1)", vi_not(q).s, vi_lit(0, q.w)), T_SGN, q.w, 2);
        nr = vi_mk(vi_fmt("add(%s, %s, cin = 1)", vi_not(r).s, vi_lit(0, r.w)), T_SGN, r.w, 2);
        diff = vi_mk(vi_fmt("%s ^ %s", vi_wrap(&sa, 1), vi_wrap(&sb, 1)), T_SL, 1, 0);
        qs = vi_mux2(q, nq, diff);
        rs = vi_mux2(r, nr, sa);
        if (isdiv) return qs;
        if (!ismod) return rs;
        /* mod: like rem, but with b's sign: rem + b when they differ and rem is not 0 */
        vi_bind(&rs);
        zero = vi_mk(vi_fmt("compare(%s, %s).eq", rs.s, vi_lit(0, rs.w)), T_BOOL, 1, 2);
        {
            VX sum = vi_mk(vi_fmt("add(%s, %s)", rs.s, vi_resize(b, rs.w, 1).s), T_SGN, rs.w, 2);
            VX fix = vi_mk(vi_fmt("(%s) & ~%s", diff.s, vi_wrap(&zero, 2)), T_BOOL, 1, 0);
            res = vi_mux2(rs, sum, fix);
            res.ty = T_SGN;
            return res;
        }
    }
}

static VX vi_term(gram_node *n)
{
    gram_node *k = n->child;
    VX acc = vi_gen_any(k, -1);
    for (k = k->next; k; k = k->next->next) {
        gram_node *rn = k->next;
        VX b;
        int ty;
        char op[4];
        snprintf(op, sizeof op, "%.*s", (int)(k->len < 3 ? k->len : 3), k->text);
        { int j; for (j = 0; op[j]; j++) op[j] = (char)vi_lower(op[j]); }
        b = vi_gen_any(rn, acc.ty == T_INT || acc.ty == T_INTV ? -1 : acc.w);
        if (acc.ty == T_INT && b.ty == T_INT) {   /* constants: worked out here */
            if (!strcmp(op, "*")) acc.iv *= b.iv;
            else if (!b.iv && strcmp(op, "**")) vi_fail(rn, "division by zero");
            else if (!strcmp(op, "/")) acc.iv /= b.iv;
            else if (!strcmp(op, "rem")) acc.iv %= b.iv;
            else if (!strcmp(op, "mod")) { long m = acc.iv % b.iv; if (m && ((m < 0) != (b.iv < 0))) m += b.iv; acc.iv = m; }
            else { long r = 1, e = b.iv; if (e < 0) vi_fail(rn, "a negative power is not an integer"); while (e--) r *= acc.iv; acc.iv = r; }
            acc.s = vi_fmt("%ld", acc.iv);
            continue;
        }
        if (!strcmp(op, "**")) {   /* 2 ** n: a 1 shifted left n places; x ** k: x * x ... */
            if (acc.ty == T_INT && acc.iv == 2 && (b.ty == T_INTV || b.ty == T_UNS)) {
                int w = b.w >= 5 ? 32 : 1 << b.w;
                if (w > 31) w = 31;
                acc = vi_barrel(vi_const(1, w, T_UNS), b, 1, 0, 0, rn);
                acc.ty = T_INTV;
                continue;
            }
            if (b.ty == T_INT && b.iv >= 1 && b.iv <= 8 && acc.ty != T_INT) {
                VX base = acc;
                long e;
                vi_bind(&base);
                for (e = 1; e < b.iv; e++) {
                    VX x = base, y = acc;
                    vi_arith2(&y, &x, n->child, rn, "**", &ty);
                    if (y.w + x.w > VI_MAXWIDE) vi_fail(k, "this power is more than %d bits", VI_MAXWIDE);
                    acc = vi_mul_vals(y, x, ty == T_INTV ? T_UNS : ty);
                    acc.ty = ty;
                    vi_bind(&acc);
                }
                continue;
            }
            vi_fail(k, "Wire works out x ** k for a constant k up to 8, and 2 ** n for a signal n");
        }
        vi_arith2(&acc, &b, n->child, rn, op, &ty);
        if (!strcmp(op, "*")) {
            if (acc.w + b.w > VI_MAXWIDE) vi_fail(k, "this product is %d bits; Wire handles up to %d", acc.w + b.w, VI_MAXWIDE);
            if (acc.w > VI_MAXWIDE / 2 || b.w > VI_MAXWIDE / 2)
                vi_fail(k, "Wire multiplies values of up to %d bits (their product has up to %d)", VI_MAXWIDE / 2, VI_MAXWIDE);
            acc = vi_mul_vals(acc, b, ty == T_INTV ? T_UNS : ty);
            acc.ty = ty;
            continue;
        }
        acc = vi_divide(acc, b, op, ty == T_INTV ? T_UNS : ty, k);
        acc.ty = ty;
    }
    return acc;
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

typedef struct { char *s; int ty, w; VSym *sym; int ut; VSym *wsym; int whi, wlo; } VTarget;   /* wsym: bits whi..wlo of a wide signal */

/* a target that is an alias: the same target, written with what the alias names */
static gram_node *vi_node_copy(const gram_node *n, gram_node *next)
{
    gram_node *c = (gram_node *)malloc(sizeof *c);
    *c = *n;
    c->next = next;
    return c;
}

static gram_node *vi_unalias(gram_node *t)
{
    gram_node *id = t->child, *e, *base, *barg = NULL, *nt;
    VSym *s;
    int guard = 0;
    if (id->next && id->next->rule == VHDL_R_field) {   /* r.f <= ...: the field's own symbol */
        gram_node *one = vi_unfield(id);
        if (!vi_find(one)) {
            VSym *r = vi_find(id);
            if (r && r->ty == T_REC) vi_fail(id->next, "%.*s has no field %.*s", VT(id), VT(id->next->child));
            vi_fail(id, "%.*s is not a record", VT(id));
        }
        nt = vi_node_copy(t, NULL);
        nt->next = t->next;
        nt->child = one;
        t = nt;
        id = one;
    }
    s = vi_find(id);
    while (s && s->aliased && guard++ < 16) {
        e = s->aliased;
        if (e->rule == VHDL_R_ident) base = e;
        else if (e->rule == VHDL_R_name && e->child->next && e->child->next->rule == VHDL_R_args && !e->child->next->next
                 && vi_nargs(e->child->next) == 1) { base = e->child; barg = e->child->next->child; }
        else vi_fail(id, "%.*s names a value, not a signal, so it cannot be given one", VT(id));
        if (barg && id->next) {   /* a part of an alias of a part: work out which bits of the signal */
            VSym *o = vi_find(base);
            long k, a, z;
            int dn;
            if (!o || id->next->rule == VHDL_R_drange || !vi_ceval(id->next, &k) || id->next->next || barg->rule != VHDL_R_drange)
                vi_fail(id->next, "give this part of %.*s a value through what %.*s names", VT(id), VT(id));
            vi_range(barg, &a, &z, &dn, "a slice");
            k = s->down ? (dn ? z + (k - vi_low(s)) : z - (k - vi_low(s))) : (dn ? a - (k - s->left) : a + (k - s->left));
            nt = vi_node_copy(t, NULL);
            nt->child = vi_node_copy(base, vi_node_copy(id->next, NULL));
            nt->child->next->rule = VHDL_R_number;
            nt->child->next->text = vi_fmt("%ld", k);
            nt->child->next->len = strlen(nt->child->next->text);
            nt->child->next->child = NULL;
            return nt;
        }
        nt = vi_node_copy(t, NULL);
        nt->child = vi_node_copy(base, barg ? vi_node_copy(barg, NULL) : id->next);
        t = nt;
        id = t->child;
        s = vi_find(id);
    }
    return t;
}

static VTarget vi_target(gram_node *t)
{
    VTarget r;
    gram_node *id;
    VSym *s;
    t = vi_unalias(t);
    id = t->child;
    s = vi_find(id);
    memset(&r, 0, sizeof r);
    if (!s) vi_fail(id, "there is no signal or port called %.*s", VT(id));
    if (s->kind == VK_IN) vi_fail(id, "%.*s is an input of the entity, so it cannot be given a value here", VT(id));
    if (s->kind == VK_CONST || s->kind == VK_GEN) vi_fail(id, "%.*s is a constant", VT(id));
    if (s->kind == VK_LABEL) vi_fail(id, "%.*s is the label of an instance, not a signal", VT(id));
    if (s->kind == VK_VAR) vi_fail(id, "%.*s is a variable; it is given a value with := inside a process", VT(id));
    r.sym = s;
    r.ut = s->ut;
    if (s->wnch) {   /* wider than 32 bits: the bits it gives, split over its Wire wires when written */
        r.wsym = s;
        r.s = s->wname;
        r.whi = s->w - 1; r.wlo = 0; r.ty = s->ty; r.w = s->w;
        if (id->next) {
            long c;
            int one;
            if (id->next->next) vi_fail(id->next->next, "one index or slice at a time, please");
            if (id->next->rule != VHDL_R_drange && !vi_ceval(id->next, &c))
                vi_fail(id->next, "picking a bit of a value wider than 32 bits with a signal is not supported yet");
            one = vi_bitrange(s, id, id->next, &r.whi, &r.wlo);
            r.ty = one ? T_SL : s->ty;
            r.w = r.whi - r.wlo + 1;
        }
        return r;
    }
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
    if (t->sym && t->sym->intneg) {   /* an integer that can be negative: kept as signed */
        if (x->ty == T_INT) { *x = vi_int_as(x, t->w, T_SGN, at); return; }
        if (x->ty == T_INTV) { *x = vi_resize(*x, t->w, 0); x->ty = T_SGN; return; }
        if (x->ty == T_SGN) { *x = vi_resize(*x, t->w, 1); return; }
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

/* the Wire for a value up to 32 bits that may be tri-state: tri(x, en) */
static char *vi_zs(const VX *x, const gram_node *at)
{
    if (x->zmix) vi_fail(at, "some bits of this value are 'Z' and some not; give the 'Z' bits and the others "
                             "to slices of the signal in two assignments");
    if (x->z) return vi_fmt("tri(%s, 0b0)", vi_lit(0, x->w));
    if (x->zen) return vi_fmt("tri(%s, %s)", x->s, x->zen);
    return x->s;
}

static int vi_idc(int c);

/* one more statement gives sym a value: several, and one that always drives it, is a conflict */
static void vi_driver(VSym *sym, const VX *x, const gram_node *at)
{
    if (!sym) return;
    sym->ndrv++;
    if (x && !x->z && !x->zen && !sym->plain) {   /* a name (or a part of one) may carry a 'Z' of its own: Wire checks those */
        const char *p = x->s;
        int name = p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_');
        if (name) {
            while (vi_idc(*p)) p++;
            if (*p == '[') { while (*p && *p != ']') p++; if (*p) p++; }
            name = !*p;
        }
        if (!name) sym->plain = at;
    }
    if (sym->ndrv > 1 && sym->plain && x)
        vi_fail(sym->plain, "%.*s is given values by several statements, but this one always drives it; give it 'Z' "
                            "when it is not talking (... else (others => 'Z'))", sym->nlen > 0 ? sym->nlen : (int)strlen(sym->wname),
                sym->nlen > 0 ? sym->name : sym->wname);
}

/* target = x: for a signal wider than 32 bits, each of its wires gets its part */
static void vi_emit_target(gram_node *at, const VTarget *t, VX x)
{
    int c;
    if (t->sym && t->s && t->sym->wname && !strcmp(t->s, t->sym->wname)) vi_driver(t->sym, &x, at);   /* the whole of it */
    if (!t->wsym) { vi_emit(at, "%s = %s", t->s, vi_zs(&x, at)); return; }
    for (c = 0; c < t->wsym->wnch; c++) {
        VSym *ch = &vi_syms[t->wsym->wch0 + c];
        int clo = 32 * c, chi = clo + ch->w - 1, olo = clo > t->wlo ? clo : t->wlo, ohi = chi < t->whi ? chi : t->whi;
        VX piece;
        if (olo > ohi) continue;
        piece = vi_bits(&x, ohi - t->wlo, olo - t->wlo);
        if (olo == clo && ohi == chi) vi_emit(at, "%s = %s", ch->wname, vi_zs(&piece, at));
        else if (olo == ohi) vi_emit(at, "%s[%d] = %s", ch->wname, olo - clo, vi_zs(&piece, at));
        else vi_emit(at, "%s[%d:%d] = %s", ch->wname, ohi - clo, olo - clo, vi_zs(&piece, at));
    }
}

static VX vi_value(gram_node *e, const VTarget *t)
{
    gram_node *v = e->rule == VHDL_R_wave ? e->child : e;
    VX x;
    vi_zok = 1;
    x = vi_gen_any(v, t->w);
    vi_zok = 0;
    if ((x.z || x.zen) && t->ty != T_SL && !vi_vec(t->ty))
        vi_fail(v, "'Z' is a value of std_logic and its vectors, but the target is %s", vi_tyname[t->ty]);
    vi_assignable(t, &x, v);
    return x;
}

/* ---- tri-state: a value with 'Z' in it is data and an enable ---------------- */

static int vi_hasz(const VX *x) { return x->z || x->zen; }

static VX vi_zen_of(const VX *x)   /* while this is 1 the value is driven */
{
    VX e;
    if (x->z) return vi_const(0, 1, T_BOOL);
    if (!x->zen) return vi_const(1, 1, T_BOOL);
    e = vi_mk(x->zen, T_BOOL, 1, 2);
    return e;
}

static VX vi_zdata(VX x)            /* the value, as it is while driven */
{
    x.z = 0;
    x.zen = NULL;
    return x;
}

static VX vi_mux2(VX d0, VX d1, VX c);

/* data, driven while en is 1 */
static VX vi_ztri(VX d, VX en)
{
    if (en.cst) {
        if (en.v & 1) return vi_zdata(d);
        return vi_zval(d.w, d.ty);
    }
    d = vi_zdata(d);
    d.zen = vi_wrap(&en, 2);
    return d;
}

/* mux(d0, d1, sel = c): d1 when c */
static VX vi_mux2(VX d0, VX d1, VX c)
{
    VX r;
    if (vi_hasz(&d0) || vi_hasz(&d1)) {   /* the data as a mux, and when it is driven as another */
        VX e, dd0, dd1;
        if (c.cst) return c.v ? d1 : d0;
        if (d0.z && d1.z) return d0;
        e = vi_mux2(vi_zen_of(&d0), vi_zen_of(&d1), c);
        dd0 = d0.z ? vi_zdata(d1) : vi_zdata(d0);   /* where it floats, the data does not matter */
        dd1 = d1.z ? vi_zdata(d0) : vi_zdata(d1);
        r = vi_mux2(dd0, dd1, c);
        return vi_ztri(r, e);
    }
    if (vi_wide(&d0)) {   /* chunk by chunk */
        VX p[2 * VI_MAXCH];
        int i, nch = (d0.w + 31) / 32;
        if (c.cst) return c.v ? d1 : d0;
        for (i = 0; i < nch; i++) p[i] = vi_mux2(vi_chunk(&d0, i), vi_chunk(&d1, i), c);
        r = vi_join(p, nch, d1.ty == T_STR ? d0.ty : d1.ty);
        return r;
    }
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

/* mux(d[0] ... d[n-1], sel = sel) for any n up to 2^sel bits: a Wire mux has
 * up to 32 inputs, so more is a mux of muxes, 32 at a time on the low bits
 * of sel; the inputs past n are 0 */
static char *vi_muxn(char **d, int n, const char *sel, int selw, int w)
{
    VBuf b = { 0, 0, 0 };
    int i, full = 1 << (selw < 31 ? selw : 30);
    char *z = vi_lit(0, w);
    if (selw <= 5) {
        vb_add(&b, "mux(", 4);
        for (i = 0; i < full; i++) vbf(&b, "%s, ", i < n ? d[i] : z);
        vbf(&b, "sel = %s)", sel);
        return b.s;
    }
    {
        int groups = (n + 31) / 32, g;
        char **top = (char **)malloc((size_t)groups * sizeof *top);
        char *lo = vi_fmt("%s[4:0]", sel), *hi = selw - 1 == 5 ? vi_fmt("%s[5]", sel) : vi_fmt("%s[%d:5]", sel, selw - 1);
        for (g = 0; g < groups; g++) {
            int m = n - 32 * g < 32 ? n - 32 * g : 32, allz = 1;
            for (i = 0; i < m; i++) if (strcmp(d[32 * g + i], z)) allz = 0;
            top[g] = allz ? z : vi_muxn(d + 32 * g, m, lo, 5, w);
        }
        return vi_muxn(top, groups, hi, selw - 5, w);
    }
}

/* mux(vals[0], vals[1] ..., sel = sel): with values that may be 'Z', the data
 * and when it is driven as two muxes (one value: that value) */
static VX vi_mux_tab(VX *vals, int n, const char *sel)
{
    VBuf b = { 0, 0, 0 };
    VX r, en, fill;
    int j, same = 1, anyz = 0;
    for (j = 0; j < n; j++) if (vi_hasz(&vals[j])) anyz = 1;
    for (j = 1; j < n; j++)
        if (strcmp(vals[j].s, vals[0].s) || vals[j].z != vals[0].z || (!vals[j].zen) != (!vals[0].zen)
            || (vals[j].zen && strcmp(vals[j].zen, vals[0].zen))) same = 0;
    if (same) return vals[0];
    if (anyz) {
        VX *d = (VX *)malloc((size_t)n * sizeof *d);
        for (j = 0; j < n && vals[j].z; j++) {}
        fill = vi_zdata(vals[j < n ? j : 0]);
        vb_add(&b, "mux(", 4);
        for (j = 0; j < n; j++) vbf(&b, "%s, ", vi_zen_of(&vals[j]).s);
        vbf(&b, "sel = %s)", sel);
        en = vi_mk(b.s, T_BOOL, 1, 2);
        for (j = 0; j < n; j++) d[j] = vals[j].z ? fill : vi_zdata(vals[j]);
        r = vi_mux_tab(d, n, sel);
        free(d);
        r.tid = vals[0].tid;
        return vi_ztri(r, en);
    }
    vb_add(&b, "mux(", 4);
    for (j = 0; j < n; j++) vbf(&b, "%s, ", vals[j].s);
    vbf(&b, "sel = %s)", sel);
    r = vi_mk(b.s, vals[0].ty, vals[0].w, 2);
    r.tid = vals[0].tid;
    return r;
}

/* a long value used more than once gets a wire of its own, so values that
 * build on each other (a loop, an if after an if) do not grow without end */

static void vi_bind(VX *x)
{
    char *w;
    if (vi_bind_at && x->zen && strlen(x->zen) >= 120) {   /* a long enable of a tri-state value */
        do w = vi_fmt("v%d_", ++vi_nbind); while (vi_wname_used(w));
        vi_emitp(vi_bind_at, "wire %s", w);
        vi_emitp(vi_bind_at, "%s = %s", w, x->zen);
        x->zen = w;
    }
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

/* the values a choice stands for, as constants of the selector's width: one,
 * or each of a range (when 0 to 3) */
static int vi_choice_vals(gram_node *ch, const VX *sel, VX **out)
{
    int ity = sel->ty == T_INTV ? T_INTV : sel->ty == T_SGN ? T_SGN : vi_num(sel->ty) ? sel->ty : T_UNS, n = 0;
    if (ch->rule == VHDL_R_drange) {   /* a range of integers */
        long a, z, v;
        int down;
        vi_range(ch, &a, &z, &down, "a choice");
        if (a > z) { long t = a; a = z; z = t; }
        if (z - a > 65535) vi_fail(ch, "this range has more than 65536 values");
        *out = (VX *)malloc((size_t)(z - a + 1) * sizeof **out);
        for (v = a; v <= z; v++) {
            VX iv;
            memset(&iv, 0, sizeof iv);
            iv.ty = T_INT; iv.cst = 1; iv.iv = v; iv.s = vi_fmt("%ld", v); iv.atom = 2;
            (*out)[n++] = vi_int_as(&iv, sel->w, ity, ch);
        }
        return n;
    }
    *out = (VX *)malloc(sizeof **out);
    (*out)[0] = vi_gen(ch, sel->w);
    if ((*out)[0].ty == T_INT) (*out)[0] = vi_int_as(&(*out)[0], sel->w, ity, ch);
    return 1;
}

/* does choice ch take integer v (a constant selector)? */
static int vi_choice_has(gram_node *ch, long v)
{
    long a, z, k;
    int down;
    if (ch->rule == VHDL_R_drange) {
        vi_range(ch, &a, &z, &down, "a choice");
        return (v >= a && v <= z) || (v >= z && v <= a);
    }
    if (!vi_ceval(ch, &k)) vi_fail(ch, "each choice must be a constant integer here");
    return k == v;
}

/* st (a when/else or with/select to a whole record) for field f alone: the
 * same statement, with the field as its target and the field of each value */
static gram_node *vi_rec_stmt(gram_node *st, gram_node *tn, const VType *rt, int f, const char *fname)
{
    gram_node *c = vi_node_copy(st, NULL), *k, *last = NULL;
    int after_target = 0;
    c->child = NULL;
    for (k = st->child; k; k = k->next) {
        gram_node *n = vi_node_copy(k, NULL);
        if (k == tn) { n->child = vi_ident_of(fname, NULL); after_target = st->rule == VHDL_R_condassign; }
        else if (after_target) { n = vi_rec_part(k, rt, f); n = vi_node_copy(n, NULL); after_target = 0; }
        else if (k->rule == VHDL_R_wclause) n->child = vi_node_copy(k->child, vi_node_copy(vi_rec_part(k->child->next, rt, f), NULL));
        else if (k->rule == VHDL_R_selarm) n->child = vi_node_copy(vi_rec_part(k->child, rt, f), k->child->next);
        if (last) last->next = n; else c->child = n;
        last = n;
    }
    return c;
}

static void vi_selassign(gram_node *st);

/* a statement to a whole record: one per field (0 if the target is not one) */
static int vi_rec_split(gram_node *st, gram_node *tn, void (*each)(gram_node *))
{
    VSym *s = tn->child->next ? NULL : vi_find(tn->child);
    VType *rt;
    int f;
    if (!s || s->ty != T_REC) return 0;
    rt = &vi_types[s->ut - 1];
    for (f = 0; f < rt->nf; f++) each(vi_rec_stmt(st, tn, rt, f, vi_syms[s->fld0 + f].name));
    return 1;
}

static void vi_condassign(gram_node *st)
{
    gram_node *k = st->child, *tn, *first;
    VTarget t;
    VX acc, vals[256], conds[256];
    int n = 0, i;
    if (k->rule == VHDL_R_label) k = k->next;
    tn = k;
    if (vi_rec_split(st, tn, vi_condassign)) return;
    {   /* a whole array: element by element */
        VSym *as = tn->child->next ? NULL : vi_find(tn->child);
        if (as && as->ty == T_ARR && !as->ramproc && as->kind != VK_CONST && as->kind != VK_IN) {
            VType *at = &vi_types[as->ut - 1];
            int e, ne = vi_nelems(at);
            first = tn->next;
            for (e = 0; e < ne; e++) {
                VSym *el = &vi_syms[as->elem0 + e];
                VTarget et;
                memset(&et, 0, sizeof et);
                et.s = el->wname; et.ty = el->ty; et.w = el->w; et.sym = el; et.ut = el->ut;
                n = 0;
                vals[0] = vi_array_elem(at, first->rule == VHDL_R_wave ? first->child : first, e, first);
                for (k = first->next; k; k = k->next) {
                    if (k->rule == VHDL_R_lastwhen) vi_fail(k, "this when has no else: the value would have to be kept, which is a latch. Add else ...");
                    if (n == 255) vi_fail(k, "too many whens");
                    conds[n] = vi_cond(k->child);
                    vals[++n] = vi_array_elem(at, k->child->next->rule == VHDL_R_wave ? k->child->next->child : k->child->next, e, k);
                }
                acc = vals[n];
                for (i = n - 1; i >= 0; i--) acc = vi_mux2(acc, vals[i], conds[i]);
                vi_emit_target(tn, &et, acc);
            }
            return;
        }
    }
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
    vi_emit_target(tn, &t, acc);
}

static void vi_selassign(gram_node *st)
{
    gram_node *k = st->child, *sn, *tn, *arm, *others = NULL;
    VTarget t;
    VX sel, *tab = NULL, oth;
    unsigned long *chosen = NULL;
    int k2, i, n, nchosen = 0;
    char *zsel = NULL;   /* some arms are 'Z': the value is driven while this is 1 */
    VBuf b = { 0, 0, 0 };
    if (k->rule == VHDL_R_label) k = k->next;
    sn = k;
    tn = sn->next;
    if (vi_rec_split(st, tn, vi_selassign)) return;
    sel = vi_gen(sn, -1);
    if (sel.ty == T_INT) {   /* a constant (a generic): only its value is given */
        t = vi_target(tn);
        for (arm = tn->next; arm; arm = arm->next) {
            gram_node *c;
            for (c = arm->child->next->child; c; c = c->next)
                if (c->rule == VHDL_R_OTHERS || vi_choice_has(c, sel.iv)) { vi_emit_target(tn, &t, vi_value(arm->child, &t)); return; }
        }
        vi_fail(st, "this with ... select does not choose %ld; add when others", sel.iv);
    }
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
            {
                VX *cvs;
                int nc = vi_choice_vals(c, &sel, &cvs), q;
                for (q = 0; q < nc; q++) {
                    cv = cvs[q];
                    if (!cv.cst) vi_fail(c, "each choice must be a constant, like \"01\"");
                    if (cv.w != k2) vi_fail(c, "the choice is %d bit%s, but %.*s is %d", cv.w, cv.w == 1 ? "" : "s", VT(sn), k2);
                    for (i = 0; i < nchosen; i++) if (chosen[i] == cv.v) vi_fail(c, "this value is chosen twice");
                    chosen = (unsigned long *)realloc(chosen, (size_t)(nchosen + 1) * sizeof *chosen);
                    chosen[nchosen++] = cv.v;
                    if (tab) tab[cv.v] = v;   /* too wide for one mux: done below, as comparisons */
                }
            }
        }
    }
    if (k2 <= 5) {
        n = 1 << k2;
        for (i = 0; i < n; i++)
            if (!tab[i].s && !tab[i].ch && !tab[i].chv) {
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
        {   /* arms that are 'Z': the data is the other arms', and when it is driven a mux of its own */
            int anyz = 0, j;
            for (i = 0; i < n; i++) if (vi_hasz(&tab[i])) anyz = 1;
            if (anyz) {
                VX fill, en;
                VBuf eb = { 0, 0, 0 };
                for (j = 0; j < n && tab[j].z; j++) {}
                if (j == n) { vi_emit_target(tn, &t, tab[0]); return; }   /* never driven */
                fill = vi_zdata(tab[j]);
                vb_add(&eb, "mux(", 4);
                for (i = 0; i < n; i++) vbf(&eb, "%s, ", vi_zen_of(&tab[i]).s);
                vbf(&eb, "sel = %s)", sel.s);
                en = vi_mk(eb.s, T_BOOL, 1, 2);
                for (i = 0; i < n; i++) tab[i] = tab[i].z ? fill : vi_zdata(tab[i]);
                zsel = vi_fmt("%s", en.s);
            }
        }
        if (t.w > MAXW) {   /* a mux per 32 bits */
            VX p[2 * VI_MAXCH];
            int c, nch = (t.w + 31) / 32;
            for (c = 0; c < nch; c++) {
                VBuf cb = { 0, 0, 0 };
                VX first = vi_chunk(&tab[0], c);
                vb_add(&cb, "mux(", 4);
                for (i = 0; i < n; i++) vbf(&cb, "%s, ", vi_chunk(&tab[i], c).s);
                vbf(&cb, "sel = %s)", sel.s);
                p[c] = vi_mk(cb.s, t.ty, first.w, 2);
            }
            {
                VX r = vi_join(p, nch, t.ty);
                if (zsel) r = vi_ztri(r, vi_mk(zsel, T_BOOL, 1, 2));
                vi_emit_target(tn, &t, r);
            }
            return;
        }
        if (!zsel) {   /* a table of constants: gates, as Logisim draws a truth table */
            VX r;
            if (vi_table_gates(&r, &sel, tab, n, t.w, t.ty)) { vi_emit_target(tn, &t, r); return; }
        }
        vb_add(&b, "mux(", 4);
        for (i = 0; i < n; i++) vbf(&b, "%s, ", tab[i].s);
        vbf(&b, "sel = %s)", sel.s);
        {
            VX r = vi_mk(b.s, t.ty, t.w, 2);
            if (zsel) r = vi_ztri(r, vi_mk(zsel, T_BOOL, 1, 2));
            vi_emit_target(tn, &t, r);
        }
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
                VX *cvs;
                int nc = vi_choice_vals(c, &sel, &cvs), q;
                for (q = 0; q < nc; q++) vbf(&cb, "%scompare(%s, %s).eq", m++ ? " | " : "", sel.s, cvs[q].s);
            }
            cond = vi_mk(cb.s, T_BOOL, 1, m > 1 ? 0 : 2);
            if (m > 1) cond = vi_mk(vi_fmt("(%s)", cb.s), T_BOOL, 1, 2);
            acc = vi_mux2(acc, v, cond);
        }
        vi_emit_target(tn, &t, acc);
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

/* a part of a port given in a port map: p(7 downto 4) => a */
typedef struct { int port, hi, lo; gram_node *actual, *at; } VPart;

/* an expression that names a signal (x, x(3), x(7 downto 4), r.f) as a target node; NULL if it is not one */
static gram_node *vi_expr_target(gram_node *v)
{
    gram_node *tg = (gram_node *)calloc(1, sizeof *tg), *a, *last = NULL;
    tg->rule = VHDL_R_target; tg->text = v->text; tg->len = v->len;
    if (v->rule == VHDL_R_ident) { tg->child = vi_node_copy(v, NULL); return tg; }
    if (v->rule != VHDL_R_name) return NULL;
    tg->child = vi_node_copy(v->child, NULL);
    last = tg->child;
    for (a = v->child->next; a; a = a->next) {
        if (a->rule == VHDL_R_field) { last->next = vi_node_copy(a, NULL); last = last->next; continue; }
        if (a->rule != VHDL_R_args || vi_nargs(a) != 1) return NULL;
        last->next = vi_node_copy(a->child, NULL);
        last = last->next;
    }
    return tg;
}

static int vi_port_part(VEnt *e, gram_node *fn, gram_node *v, VPart **parts, int *n)
{
    int i, hi, lo;
    for (i = 0; i < e->nports; i++)
        if (e->ports[i].nlen == (int)fn->len && vi_is(fn, vi_fmt("%.*s", e->ports[i].nlen, e->ports[i].name))) break;
    if (i == e->nports) return 0;
    if (fn->next->next) vi_fail(fn->next->next, "one index or slice at a time, please");
    if (e->ports[i].inout) vi_fail(fn, "an inout port is connected whole");
    if (!vi_vec(e->ports[i].ty)) vi_fail(fn, "%.*s is one bit; connect it whole", VT(fn));
    vi_bitrange(&e->ports[i], fn, fn->next, &hi, &lo);
    *parts = (VPart *)realloc(*parts, (size_t)(*n + 1) * sizeof **parts);
    (*parts)[*n].port = i; (*parts)[*n].hi = hi; (*parts)[*n].lo = lo; (*parts)[*n].actual = v; (*parts)[(*n)++].at = fn;
    return 1;
}

static int vi_part_cmp(const void *a, const void *b) { return ((const VPart *)a)->lo - ((const VPart *)b)->lo; }

/* port i given in parts: an input is its parts side by side, an output gives each part to its signal */
static void vi_port_parts(VEnt *e, int i, VSym *inst, VPart *parts, int nparts, VBuf *call, VBuf *outs, gram_node *st)
{
    VSym *p = &e->ports[i];
    VPart mine[64];
    int n = 0, k, next = 0;
    VBuf b = { 0, 0, 0 };
    for (k = 0; k < nparts; k++) if (parts[k].port == i && n < 64) mine[n++] = parts[k];
    qsort(mine, (size_t)n, sizeof *mine, vi_part_cmp);
    for (k = 0; k < n; k++) {
        if (mine[k].lo < next) vi_fail(mine[k].at, "bit %d of port %.*s is connected twice", mine[k].lo, p->nlen, p->name);
        if (p->kind == VK_IN && mine[k].lo > next)
            vi_fail(st, "bit%s %d of input %.*s %s not connected", mine[k].lo - 1 > next ? "s" : "", next, p->nlen, p->name,
                    mine[k].lo - 1 > next ? "are" : "is");
        next = mine[k].hi + 1;
    }
    if (p->kind == VK_IN) {
        if (next < p->w) vi_fail(st, "bits %d to %d of input %.*s are not connected", next, p->w - 1, p->nlen, p->name);
        vb_add(&b, "{", 1);
        for (k = n - 1; k >= 0; k--) {
            int w = mine[k].hi - mine[k].lo + 1;
            VX x = vi_gen(mine[k].actual, w);
            if (x.ty == T_INT || x.w != w) vi_fail(mine[k].actual, "this part of %.*s is %d bit%s, but this is %s", p->nlen, p->name,
                                                    w, w == 1 ? "" : "s", vi_desc(&x));
            vbf(&b, "%s%s", k == n - 1 ? "" : ", ", x.s);
        }
        vb_add(&b, "}", 1);
        vbf(call, "%s%s = %s", call->s[call->n - 1] == '(' ? "" : ", ", p->wname, n == 1 ? vi_fmt("%.*s", (int)strlen(b.s) - 2, b.s + 1) : b.s);
        return;
    }
    for (k = 0; k < n; k++) {
        int w = mine[k].hi - mine[k].lo + 1;
        gram_node *tn = mine[k].actual->rule == VHDL_R_OPEN ? NULL : vi_expr_target(mine[k].actual);
        VTarget t;
        if (mine[k].actual->rule == VHDL_R_OPEN) continue;
        if (!tn) vi_fail(mine[k].actual, "a part of output %.*s must go to a signal (or open)", p->nlen, p->name);
        t = vi_target(tn);
        if (t.wsym) vi_fail(mine[k].actual, "give a part of output %.*s to a signal of up to 32 bits", p->nlen, p->name);
        if (t.w != w) vi_fail(mine[k].actual, "this part of %.*s is %d bit%s, but this is %d", p->nlen, p->name, w, w == 1 ? "" : "s", t.w);
        if (w == 1) vbf(outs, "; %s = %s.%s[%d]", t.s, inst->wname, p->wname, mine[k].lo);
        else vbf(outs, "; %s = %s.%s[%d:%d]", t.s, inst->wname, p->wname, mine[k].hi, mine[k].lo);
    }
}

/* fa: entity work.full_adder port map(a => x, b => y, cin => c, s => s0, cout => c1);
 * becomes  fa = full_adder(a = x, b = y, cin = c); s0 = fa.s; c1 = fa.cout */
static void vi_inst(gram_node *st)
{
    gram_node *label = st->child->child, *en = vi_inst_name(st), *pm = vi_kid(st, VHDL_R_pmap), *gm = vi_kid(st, VHDL_R_gmap), *as;
    VEnt *e = vi_ent_named(en);
    VSym *inst;
    gram_node **actual;
    int i, pos = 0, named = 0, nparts = 0, nlater = 0;
    VPart *parts = NULL;
    VTarget later_t[16];
    VX later_v[16];
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
            if (e->gens[g].ty != T_INT) {   /* a boolean, bit or vector */
                VX x = vi_gen(v, e->gens[g].w);
                VTarget tt;
                memset(&tt, 0, sizeof tt);
                tt.s = vi_fmt("%.*s", e->gens[g].nlen, e->gens[g].name); tt.ty = e->gens[g].ty; tt.w = e->gens[g].w;
                if (!x.cst) vi_fail(v, "a generic's value must be a constant");
                vi_assignable(&tt, &x, v);
                val[g] = (long)x.v;
            } else if (!vi_ceval(v, &val[g])) vi_fail(v, "a generic's value must be a constant");
            if (val[g] != e->gens[g].ival) differ = 1;
        }
        pos = 0;
        if (differ) e = vi_copy(e, val, st);
    }
    actual = (gram_node **)calloc((size_t)e->nports + 1, sizeof *actual);
    for (as = pm->child; as; as = as->next) {
        gram_node *f = as->child->next ? as->child : NULL, *v = f ? f->next : as->child;
        if (f) {
            gram_node *fn = f->child->next && f->child->next->rule == VHDL_R_field ? vi_unfield(f->child) : f->child;
            named = 1;
            if (fn->next) {   /* a part of a port: its bits, gathered below */
                if (vi_port_part(e, fn, v, &parts, &nparts)) continue;
                vi_fail(f, "%.*s has no port called %.*s", VT(e->ent->child), VT(fn));
            }
            for (i = 0; i < e->nports; i++)
                if (e->ports[i].nlen == (int)fn->len && vi_is(fn, vi_fmt("%.*s", e->ports[i].nlen, e->ports[i].name))) break;
            if (i == e->nports) vi_fail(f, "%.*s has no port called %.*s", VT(e->ent->child), (int)fn->len, fn->text);
        } else {
            int q;
            if (named) vi_fail(v, "put the ports without a name (positional) before the named ones");
            for (i = 0, q = 0; i < e->nports; i++) {   /* the fields of a record port are not ports of their own here */
                if (e->ports[i].nlen < 0 || memchr(e->ports[i].name, '.', (size_t)e->ports[i].nlen)) continue;
                if (q++ == pos) break;
            }
            if (i == e->nports) vi_fail(v, "%.*s has only %d ports", VT(e->ent->child), q);
            pos++;
        }
        if (actual[i]) vi_fail(as, "port %.*s is connected twice", e->ports[i].nlen, e->ports[i].name);
        actual[i] = v;
        if (e->ports[i].ty == T_REC && v->rule != VHDL_R_OPEN) {   /* a record: each field gets its part of what it is given */
            int j, k = 0;
            for (j = i + 1; j < e->nports && e->ports[j].nlen > e->ports[i].nlen && e->ports[j].name[e->ports[i].nlen] == '.'; j++, k++) {
                const char *fname = e->ports[j].name + e->ports[i].nlen + 1;
                gram_node *one = v->rule == VHDL_R_ident ? v : v->rule == VHDL_R_name ? vi_unfield(v->child) : NULL;
                if (one && !one->next) {
                    gram_node *fa = vi_ident_of(vi_fmt("%.*s.%s", (int)one->len, one->text, fname), NULL);
                    VSym *fs = vi_find(fa);
                    if (!fs) vi_fail(v, "%.*s has no field %s", (int)one->len, one->text, fname);
                    actual[j] = fa;
                } else if (v->rule == VHDL_R_aggregate) {
                    VType fake;
                    memset(&fake, 0, sizeof fake);
                    fake.nf = k + 1;
                    fake.fid = (gram_node **)calloc((size_t)k + 1, sizeof *fake.fid);
                    fake.fid[k] = vi_ident_of(fname, NULL);
                    actual[j] = vi_rec_part(v, &fake, k);
                } else vi_fail(v, "port %.*s is a record: give it a record signal or an aggregate", e->ports[i].nlen, e->ports[i].name);
            }
        }
    }
    inst = vi_declare(label, VK_LABEL);
    vbf(&call, "%s = %s(", inst->wname, e->wname);
    for (i = 0; i < e->nports; i++) {
        VSym *p = &e->ports[i];
        gram_node *v = actual[i];
        if (p->ty == T_REC) continue;   /* its fields are its pins */
        if (p->nlen < 0) continue;      /* a part of 32 bits of a wider port: done with it */
        if (p->wnch) {   /* wider than 32 bits: its pins, a part of 32 bits each */
            int c;
            if (!v || v->rule == VHDL_R_OPEN) {
                if (p->kind == VK_IN) vi_fail(st, "input %.*s of %.*s is not connected", p->nlen, p->name, VT(e->ent->child));
                continue;
            }
            if (p->kind == VK_IN) {
                VX x = vi_gen_any(v, p->w);
                VTarget tt;
                memset(&tt, 0, sizeof tt);
                tt.s = vi_fmt("%.*s", p->nlen, p->name); tt.ty = p->ty; tt.w = p->w;
                vi_assignable(&tt, &x, v);
                for (c = 0; c < p->wnch; c++) {
                    VX ch = vi_chunk(&x, c);
                    vbf(&call, "%s%s = %s", call.s[call.n - 1] == '(' ? "" : ", ", e->ports[i + 1 + c].wname, ch.s);
                }
            } else {
                gram_node *tn = vi_expr_target(v);
                VTarget t;
                VX parts[VI_MAXCH];
                if (!tn) vi_fail(v, "output %.*s of %.*s must go to a signal (or open)", p->nlen, p->name, VT(e->ent->child));
                t = vi_target(tn);
                if (t.w != p->w) vi_fail(v, "port %.*s of %.*s is %d bits, but this is %d", p->nlen, p->name, VT(e->ent->child), p->w, t.w);
                for (c = 0; c < p->wnch; c++) parts[c] = vi_mk(vi_fmt("%s.%s", inst->wname, e->ports[i + 1 + c].wname), p->ty, e->ports[i + 1 + c].w, 2);
                if (nlater < 16) { later_t[nlater] = t; later_v[nlater++] = vi_join(parts, p->wnch, p->ty); }
                else vi_fail(v, "more than 16 wide outputs on one instance");
            }
            continue;
        }
        if (!v && nparts) {   /* given in parts: p(3 downto 0) => a, p(7 downto 4) => b */
            int q;
            for (q = 0; q < nparts && parts[q].port != i; q++) {}
            if (q < nparts) { vi_port_parts(e, i, inst, parts, nparts, &call, &outs, st); continue; }
        }
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
            if (p->inout) {   /* what it reads is the signal it drives, with every other driver of it */
                VX x = !v || v->rule == VHDL_R_OPEN ? vi_const(0, p->w, p->ty == T_SL ? T_SL : T_STR) : vi_gen(v, p->w);
                vi_port_fits(p, e, x.ty, x.w, v ? v : st, 0);
                vbf(&call, "%s%s = %s", call.s[call.n - 1] == '(' ? "" : ", ", p->rname, x.s);
            }
            if (!v || v->rule == VHDL_R_OPEN) continue;
            if (v->rule != VHDL_R_ident && v->rule != VHDL_R_name)
                vi_fail(v, "output %.*s of %.*s must go to a signal (or open)", p->nlen, p->name, VT(e->ent->child));
            {   /* the signal as a target: x, x(3), x(7 downto 4) */
                gram_node *id = v->rule == VHDL_R_ident ? v : v->child;
                VSym *s = vi_find(id);
                if (!s || s->kind > VK_SIG) vi_fail(id, "%.*s is not a signal or port", VT(id));
                if (s->kind == VK_IN) vi_fail(id, "%.*s is an input, so it cannot be driven by %.*s", VT(id), VT(label));
                if (s->wnch) {   /* part of a signal wider than 32 bits: a wire of its own, then each part */
                    int hi = s->w - 1, lo = 0, one = 0, c;
                    char *tmp;
                    if (v->rule == VHDL_R_name) {
                        gram_node *args = id->next;
                        if (args->rule != VHDL_R_args || args->next || vi_nargs(args) != 1)
                            vi_fail(v, "output %.*s of %.*s must go to a signal, a bit or a slice", p->nlen, p->name, VT(e->ent->child));
                        one = vi_bitrange(s, id, args->child, &hi, &lo);
                    }
                    t.ty = one ? T_SL : s->ty; t.w = hi - lo + 1;
                    vi_port_fits(p, e, t.ty, t.w, v, 1);
                    tmp = vi_fresh("o");
                    vbf(&outs, "; wire %s[%d]; %s = %s.%s", tmp, t.w, tmp, inst->wname, p->wname);
                    for (c = 0; c < s->wnch; c++) {
                        VSym *ch = &vi_syms[s->wch0 + c];
                        int clo = 32 * c, chi = clo + ch->w - 1, olo = clo > lo ? clo : lo, ohi = chi < hi ? chi : hi;
                        if (olo > ohi) continue;
                        if (olo == clo && ohi == chi) vbf(&outs, "; %s", ch->wname);
                        else if (olo == ohi) vbf(&outs, "; %s[%d]", ch->wname, olo - clo);
                        else vbf(&outs, "; %s[%d:%d]", ch->wname, ohi - clo, olo - clo);
                        if (ohi == olo) vbf(&outs, " = %s[%d]", tmp, ohi - lo);
                        else if (ohi - olo + 1 == t.w) vbf(&outs, " = %s", tmp);
                        else vbf(&outs, " = %s[%d:%d]", tmp, ohi - lo, olo - lo);
                    }
                    continue;
                } else if (v->rule == VHDL_R_name) {
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
    for (i = 0; i < nlater; i++) vi_emit_target(label, &later_t[i], later_v[i]);   /* the wide outputs, after the instance */
}

static void vi_statement(gram_node *st);
static int vi_ndrv;
static void vi_drv_scan(gram_node *first, int mult);
static int vi_drivers(const char *name, int n);
static const gram_node *vi_cpu_at;   /* attribute wire_cpu: its value, and the text of it */
static char *vi_cpu_spec;
static void vi_emit_cpu(const VEnt *e);

/* what the packages an entity uses declare: its types, constants, functions and procedures */
static void vi_set_ctx(int ctx);

static void vi_use_pkg(int p, int mark)
{
    VPkg *k = &vi_pkgs[p];
    gram_node *d;
    int i, ctx = vi_ctx;
    if (k->mark == mark) return;   /* declared already */
    k->mark = mark;
    for (i = 0; i < k->ndeps; i++) vi_use_pkg(k->deps[i], mark);   /* the packages it uses, first */
    for (d = k->spec->child->next; d; d = d->next)
        if (d->rule == VHDL_R_signal) vi_fail(d, "a signal in a package is not supported; declare it in the architecture");
    vi_set_ctx(k->ctx);
    vi_decls(k->spec->child->next);
    if (k->body) vi_decls(k->body->child->next);
    vi_set_ctx(ctx);
}

static void vi_use_pkgs(const VEnt *e)
{
    static int mark;
    int i;
    mark++;
    for (i = 0; i < e->npkgs; i++) vi_use_pkg(e->pkgs[i], mark);
    vi_scope = vi_nsyms;   /* the entity's own names may hide the packages' */
}

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
    vi_nfuncs = 0;
    vi_scope = 0;
    vi_use_pkgs(e);
    vi_ports(vi_kid(e->ent, VHDL_R_generics), 1, 0);
    vi_ports(vi_kid(e->ent, VHDL_R_ports), 0, 0);
    e->nports = e->ngens = 0;
    for (k = vi_scope; k < vi_nsyms; k++) if (vi_syms[k].kind == VK_IN || vi_syms[k].kind == VK_OUT) e->nports++;
    e->gens = (VSym *)calloc((size_t)vi_nsyms + 1, sizeof *e->gens);
    for (k = vi_scope; k < vi_nsyms; k++) if (vi_syms[k].kind == VK_GEN) e->gens[e->ngens++] = vi_syms[k];
    e->ports = (VSym *)calloc((size_t)e->nports + 1, sizeof *e->ports);
    e->nports = 0;
    for (k = vi_scope; k < vi_nsyms; k++)
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
    vi_scope = 0;
}

static int vi_wname_taken(const char *w)   /* a component name already in use (or a part of Wire) */
{
    static const char *parts[] = { "and", "or", "nand", "nor", "xor", "xnor", "not", "buf", "mux", "demux", "decoder",
                                   "encoder", "add", "mul", "compare", "dff", "reg", "tff", "jkff", "counter", "tri", "rom",
                                   "ram", "screen", NULL };
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
    if (vi_nents - vi_nbase >= 1024) vi_fail(at, "too many different generic maps (at most 1024)");
    c = (VEnt *)calloc(1, sizeof *c);
    c->ent = e->ent; c->arch = e->arch; c->used = 1; c->over = val; c->ctx = e->ctx;
    memcpy(c->pkgs, e->pkgs, sizeof c->pkgs);
    c->npkgs = e->npkgs;
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

/* an output (or inout) driven by several statements is a bus inside, given
 * to the port; at the top an inout is a bus the page drives too (p_in while
 * p_oe), and inside a chip an inout nothing drives floats */
static void vi_port_buses(VEnt *e)
{
    int i, n = vi_nsyms;
    for (i = 0; i < n; i++) {
        VSym *s = &vi_syms[i];
        int drv;
        const char *w = vi_vec(s->ty) ? vi_fmt("[%d]", s->w) : "";
        char *b;
        if (s->kind != VK_OUT || s->ty == T_REC) continue;
        drv = vi_drivers(s->name, s->nlen);
        if (!s->inout && drv < 2) continue;
        if (s->inout && !e->top && drv == 0) { vi_emit(s->at, "%s = tri(%s, 0b0)", s->wname, vi_lit(0, s->w)); continue; }
        if (s->inout && !e->top && drv == 1) continue;
        b = vi_fmt("%s_bus", s->wname);
        while (vi_wname_used(b)) b = vi_fmt("%s_", b);
        vi_emit(s->at, "bus %s%s", b, w);
        vi_emit(s->at, "%s = %s", s->wname, b);
        if (s->inout && e->top) {
            vi_emit(s->at, "%s = tri(%s, %s)", b, s->rname, s->roe);
            s->rname = b;
        }
        s->wname = b;
    }
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
    vi_scope = 0;
    vi_suffix[0] = 0;
    vi_nclk = 0;
    vi_use_pkgs(e);
    if (e->top) vi_scan_clocks(e->arch->child->next->next);   /* their ports become clock */
    if (e->top) vi_emit(e->ent, "circuit \"%.*s\"", VT(e->ent->child));
    vi_ports(vi_kid(e->ent, VHDL_R_generics), 1, 0);
    vi_ndrv = 0;
    vi_drv_scan(e->arch->child->next->next, 1);   /* with the generics known, for if ... generate */
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
                const char *pn = p->wname;
                if (p->ty == T_REC || p->wnch) continue;   /* its fields (or its parts of 32 bits) are its pins */
                if (p->inout && !o) pn = p->rname;   /* an inout: read through its input pin */
                else if ((p->kind == VK_OUT) != o) continue;
                vbf(&h, "%s%s", first ? "" : ", ", pn);
                if (vi_vec(p->ty)) vbf(&h, "[%d]", p->w);
                first = 0;
            }
        }
        vb_add(&h, ") {", 3);
        vi_emit(e->ent, "%s", h.s);
    }
    vi_port_buses(e);
    vi_decls(e->arch->child->next->next);
    for (k = e->arch->child->next->next; k; k = k->next) {
        if (vi_is_decl(k->rule) || k->rule == VHDL_R_ident) continue;
        vi_statement(k);
    }
    if (vi_cpu_spec && vi_cpu_at->text >= e->arch->text && vi_cpu_at->text < e->arch->text + e->arch->len) vi_emit_cpu(e);
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
        int down;
        vi_range(r, &from, &to, &down, "the range of a generate");
        step = down ? -1 : 1;
        if ((to - from) * step < 0) return;   /* an empty range */
        if ((to - from) * step > 16383) vi_fail(r, "this generate makes more than 16384 copies");
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

static VX vi_retc(const VEnv *e) { return e->ret.s ? e->ret : vi_const(1, 1, T_BOOL); }

static VX vi_and1(VX a, VX b)   /* a and b, of one bit each */
{
    if (a.cst) return a.v & 1 ? b : a;
    if (b.cst) return b.v & 1 ? a : b;
    return vi_mk(vi_fmt("%s & %s", vi_wrap(&a, 1), vi_wrap(&b, 1)), T_BOOL, 1, 0);
}

/* the bits hi..lo of old replaced by v */
static VX vi_splice(VX old, int w, int hi, int lo, VX v)
{
    VBuf b = { 0, 0, 0 };
    VX r;
    if (hi == w - 1 && lo == 0) return v;
    if (vi_hasz(&old) || vi_hasz(&v)) {   /* part 'Z' and part not: said where it is given to the signal */
        r = vi_const(0, w, old.ty);
        r.zmix = 1;
        return r;
    }
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
    m.ret = vi_mux2(vi_retc(f), vi_retc(t), c);
    if (m.ret.cst && (m.ret.v & 1)) m.ret.s = NULL;
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
static VX vi_func_elem(gram_node *v, VType *t, int i);

static VX vi_array_elem(VType *t, gram_node *v, int i, gram_node *at)
{
    long idx = t->alo + i;
    VX x;
    VTarget tt;
    memset(&tt, 0, sizeof tt);
    tt.s = "an element"; tt.ty = t->elem.ty; tt.w = t->elem.w; tt.ut = t->elem.ut;
    if (v->rule == VHDL_R_ident && !vi_find(v) && vi_find_func(v, 0)) return vi_func_elem(v, t, i);   /* f, with no arguments */
    if (v->rule == VHDL_R_ident) {
        VSym *a = vi_find(v);
        if (!a || a->ty != T_ARR) vi_fail(v, "this is not an array of the same type");
        {
            VType *at2 = &vi_types[a->ut - 1];
            if (at2 != t && (vi_nelems(at2) != vi_nelems(t) || at2->elem.w != t->elem.w || at2->two != t->two))
                vi_fail(v, "this is not an array of the same type");
        }
        return vi_elem_val(a, i, v);
    }
    if (v->rule == VHDL_R_name && v->child->next && v->child->next->rule == VHDL_R_args && !v->child->next->next
        && vi_find_func(v->child, vi_nargs(v->child->next))) {   /* a function that returns an array */
        return vi_func_elem(v, t, i);
    }
    if (t->two && v->rule == VHDL_R_aggregate) {   /* ((row 0), (row 1) ...) or (others => (others => '0')) */
        int cols = t->bhi - t->blo + 1;
        VType row = *t;
        gram_node *el, *found = NULL, *others = NULL;
        int pos = 0, r = i / cols;
        for (el = v->child; el; el = el->next) {
            gram_node *ch = el->child->rule == VHDL_R_choices ? el->child : NULL, *e = ch ? ch->next : el->child, *c;
            if (!ch) { if (pos++ == r) found = e; continue; }
            for (c = ch->child; c; c = c->next) {
                long k;
                if (c->rule == VHDL_R_OTHERS) others = e;
                else if (!vi_ceval(c, &k)) vi_fail(c, "a choice in an array aggregate must be a constant index");
                else if (k == t->alo + r) found = e;
            }
        }
        if (!found) found = others;
        if (!found) vi_fail(v, "row %ld of this aggregate has no value; add others => ...", t->alo + r);
        row.two = 0; row.alo = t->blo; row.ahi = t->bhi;
        return vi_array_elem(&row, found, i % cols, at);
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

/* can index d (a signal ix[d] of some bits) be v? */
static int isc_ok(const VX *ix, const long *k, int d, long v)
{
    (void)k;
    return v >= 0 && (ix[d].w >= 31 || v < (1L << ix[d].w));
}

static void vi_seq_assign(gram_node *target, gram_node *val, int isvar)
{
    gram_node *id = (target = vi_unalias(target))->child, *a1 = id->next, *a2 = a1 ? a1->next : NULL;
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
    if (!isvar && s->kind == VK_VAR && !s->sigparam) vi_fail(id, "%.*s is a variable; give it a value with :=", VT(id));
    si = (int)(s - vi_syms);
    if (s->ty == T_REC) {   /* a whole record: field by field */
        VType *rt = &vi_types[s->ut - 1];
        int f;
        if (a1) vi_fail(a1, "%.*s is a record; give its fields values, like %.*s.%.*s", VT(id), VT(id), VT(rt->fid[0]));
        for (f = 0; f < rt->nf; f++) {
            gram_node *tg = vi_node_copy(target, NULL);
            tg->child = vi_ident_of(vi_syms[vi_syms[si].fld0 + f].name, NULL);
            vi_seq_assign(tg, vi_rec_part(v, rt, f), isvar);
        }
        return;
    }
    if (s->wnch) {   /* wider than 32 bits: each of its signals (or variables) gets its part */
        int hi = s->w - 1, lo = 0, one = 0, c;
        if (a1) {
            long cc;
            if (a2) vi_fail(a2, "one index or slice at a time, please");
            if (a1->rule != VHDL_R_drange && !vi_ceval(a1, &cc)) {   /* a bit picked by a signal: each bit, when it is its own */
                VX ix = vi_gen(a1, -1);
                int lo2 = vi_low(s), hi2 = vi_high(s);
                if (ix.ty != T_INTV && ix.ty != T_UNS) vi_fail(a1, "an index is an integer: write %.*s(to_integer(...))", VT(id));
                tt.s = s->wname; tt.ty = T_SL; tt.w = 1; tt.sym = s;
                x = vi_gen(v, 1);
                vi_assignable(&tt, &x, v);
                vi_bind(&x); vi_bind(&ix);
                for (c = 0; c < vi_syms[si].wnch; c++) {
                    int ci = vi_syms[si].wch0 + c, k, cw = vi_syms[ci].w;
                    VSlot *sl = vi_slot(vi_env, ci);
                    VX old = sl->v, nv;
                    VBuf b = { 0, 0, 0 };
                    vi_bind(&old);
                    vb_add(&b, "{", 1);
                    for (k = cw - 1; k >= 0; k--) {
                        int bit = 32 * c + k;   /* counted from the right end of s */
                        long j = s->down ? bit + lo2 : hi2 - bit;
                        VX ob = old.cst ? vi_const((old.v >> k) & 1, 1, T_SL) : vi_mk(vi_sl(vi_wrap(&old, 2), k, k), T_SL, 1, 2), nb = ob;
                        if (ix.w >= 31 || (j >= 0 && j < (1L << ix.w))) {
                            nb = vi_mux2(ob, x, vi_eq_code(ix, (unsigned long)j));
                            if (!vi_live(vi_env)) nb = vi_mux2(ob, nb, vi_env->live);
                        }
                        vbf(&b, "%s%s", k == cw - 1 ? "" : ", ", nb.s);
                    }
                    vb_add(&b, "}", 1);
                    nv = cw == 1 ? vi_mk(vi_fmt("%.*s", (int)strlen(b.s) - 2, b.s + 1), vi_syms[ci].ty, 1, 2) : vi_mk(b.s, vi_syms[ci].ty, cw, 2);
                    nv.cat = cw > 1;
                    sl->v = nv;
                }
                return;
            }
            one = vi_bitrange(s, id, a1, &hi, &lo);
        }
        tt.s = s->wname; tt.ty = one ? T_SL : s->ty; tt.w = hi - lo + 1; tt.sym = s;
        vi_zok = !isvar && !a1;
        x = vi_gen_any(v, tt.w);
        vi_zok = 0;
        vi_assignable(&tt, &x, v);
        for (c = 0; c < vi_syms[si].wnch; c++) {
            int ci = vi_syms[si].wch0 + c, clo = 32 * c, chi = clo + vi_syms[ci].w - 1;
            int olo = clo > lo ? clo : lo, ohi = chi < hi ? chi : hi, whole;
            VX piece;
            if (olo > ohi) continue;
            piece = vi_bits(&x, ohi - lo, olo - lo);
            piece.ty = vi_syms[ci].ty;
            whole = olo == clo && ohi == chi;
            vi_put(vi_env, ci, piece, whole ? -1 : ohi - clo, whole ? -1 : olo - clo);
        }
        return;
    }
    if (s->ty == T_ARR && s->ramproc) return;   /* a ram's write: its ram part, made already */
    if (s->ty == T_ARR) {
        VType *t = &vi_types[s->ut - 1];
        int i, n = vi_nelems(t), e0 = s->elem0, cols = t->two ? t->bhi - t->blo + 1 : 1, d, nd = t->two ? 2 : 1, allc = 1;
        long k[2];
        gram_node *ixn[2];
        VX ix[2];
        int isc[2] = { 1, 1 };
        if (!a1) {   /* the whole array */
            for (i = 0; i < n; i++) vi_put(vi_env, e0 + i, vi_array_elem(t, v, i, v), -1, -1);
            return;
        }
        ixn[0] = a1; ixn[1] = a2;
        if (nd == 2 && !a2) vi_fail(a1, "%.*s has two indexes: give an element a value, like %.*s(0, 0)", VT(id), VT(id));
        for (d = 0; d < nd; d++) {
            int lo = d ? t->blo : t->alo, hi = d ? t->bhi : t->ahi;
            if (ixn[d]->rule == VHDL_R_drange) vi_fail(ixn[d], "give one element of %.*s a value, not a slice", VT(id));
            if (vi_ceval(ixn[d], &k[d])) {
                if (k[d] < lo || k[d] > hi) vi_fail(ixn[d], "%ld is outside %.*s(%d to %d)", k[d], VT(id), lo, hi);
                continue;
            }
            allc = 0;
            isc[d] = 0;
            ix[d] = vi_gen(ixn[d], -1);
            if (ix[d].ty != T_INTV && ix[d].ty != T_UNS) vi_fail(ixn[d], "an index is an integer: write %.*s(to_integer(...))", VT(id));
            vi_bind(&ix[d]);
        }
        a2 = nd == 2 ? a2->next : a2;
        if (!allc) {   /* an element picked by a signal: each element, when the index is its own */
            int hi = t->elem.w - 1, lo = 0, part = 0;
            if (a2) {   /* a bit or slice of that element */
                if (a2->next) vi_fail(a2->next, "one index or slice at a time, please");
                part = 1;
                vi_bitrange(&t->elem, id, a2, &hi, &lo);
            }
            tt.s = "an element"; tt.ty = part && hi == lo ? T_SL : t->elem.ty; tt.w = hi - lo + 1; tt.ut = t->elem.ut;
            x = vi_gen(v, tt.w);
            vi_assignable(&tt, &x, v);
            vi_bind(&x);
            for (i = 0; i < n; i++) {
                long r = t->alo + i / cols, c = t->blo + i % cols;
                VX cond, old;
                if (isc[0] ? r != k[0] : !isc_ok(ix, k, 0, r)) continue;
                if (nd == 2 && (isc[1] ? c != k[1] : !isc_ok(ix, k, 1, c))) continue;
                if (nd == 2 && !isc[0] && !isc[1]) {
                    VX c0 = vi_eq_code(ix[0], (unsigned long)r), c1 = vi_eq_code(ix[1], (unsigned long)c);
                    cond = vi_mk(vi_fmt("%s & %s", vi_wrap(&c0, 1), vi_wrap(&c1, 1)), T_BOOL, 1, 0);
                } else cond = vi_eq_code(ix[nd == 2 && isc[0] ? 1 : 0], (unsigned long)(nd == 2 && isc[0] ? c : r));
                if (!part) { vi_put_when(vi_env, e0 + i, x, cond); continue; }
                old = vi_slot(vi_env, e0 + i)->v;
                vi_bind(&old);
                vi_put_when(vi_env, e0 + i, vi_splice(old, t->elem.w, hi, lo, x), cond);
            }
            return;
        }
        si = e0 + (int)((k[0] - t->alo) * cols + (nd == 2 ? k[1] - t->blo : 0));
        s = &vi_syms[si];
        a1 = a2; a2 = a1 ? a1->next : NULL;
    }
    if (!a1) {
        tt.s = s->wname; tt.ty = s->ty; tt.w = s->w; tt.sym = s; tt.ut = s->ut;
        vi_zok = !isvar && (s->ty == T_SL || vi_vec(s->ty));
        x = vi_gen(v, s->w);
        vi_zok = 0;
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
    vi_bind(&c);   /* a long condition gets a wire: every value it picks between uses it */
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
    if (sel.ty == T_ARR) vi_fail(seln, "case selects on a signal: a vector, an integer or a state");
    if (sel.ty == T_INT) {   /* a constant (a generic): only its branch is taken */
        for (w = seln->next; w && w->rule == VHDL_R_casew; w = w->next)
            for (ch = w->child->child; ch; ch = ch->next)
                if (ch->rule == VHDL_R_OTHERS || vi_choice_has(ch, sel.iv)) { vi_seq(w->child->next->child); return; }
        vi_fail(st, "this case does not choose %ld; add when others => ...", sel.iv);
    }
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
            {
                VX *cvs;
                int nc = vi_choice_vals(ch, &sel, &cvs), q;
                for (q = 0; q < nc; q++) {
                    cv = cvs[q];
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
            }
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
                if (pass == nb) {   /* live, the last time round (and ret) */
                    VX *rv = (VX *)malloc((size_t)n * sizeof *rv);
                    for (j = 0; j < n; j++) { vals[j] = envs[own[j]].live; rv[j] = vi_retc(&envs[own[j]]); }
                    m.ret = vi_mux_tab(rv, n, sel.s);
                    if (m.ret.cst && (m.ret.v & 1)) m.ret.s = NULL;
                    free(rv);
                } else {
                    if (vi_slot_find(&m, si)) { free(vals); continue; }
                    for (j = 0; j < n; j++) {
                        VSlot *sl = vi_slot_find(&envs[own[j]], si);
                        unsigned long mk;
                        if (sl) { vals[j] = sl->v; mk = sl->mask; } else vals[j] = vi_slot_init(si, &mk);
                        mask &= mk;
                    }
                }
                (void)same; (void)bb;
                vals[0] = vi_mux_tab(vals, n, sel.s);
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
    {
        int down;
        vi_range(r, &from, &to, &down, "the range of a loop");
        step = down ? -1 : 1;
    }
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
    vi_env->live = vi_and1(live, vi_retc(vi_env));   /* after the loop, everything runs again (unless it returned) */
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

static long vi_epoch;            /* counts statements read: a function's array result is worked out once per statement */
static int vi_ret_si = -1;       /* inside a function: the VSym its return value goes to; -2 in a procedure */

static void vi_seq_return(gram_node *st)
{
    VSym *r;
    VX x, was = vi_env ? vi_env->live : vi_const(1, 1, T_BOOL);
    VTarget tt;
    if (vi_ret_si == -1) vi_fail(st, "return belongs inside a function or a procedure");
    if (vi_ret_si == -2) {   /* a procedure: nothing after it runs */
        if (st->child) vi_fail(st->child, "a procedure returns no value");
    } else if (!st->child) vi_fail(st, "a function returns a value: return ...;");
    else {
        r = &vi_syms[vi_ret_si];
        if (r->ty == T_ARR) {   /* an array: element by element */
            VType *t = &vi_types[r->ut - 1];
            int i, n = vi_nelems(t);
            for (i = 0; i < n; i++) {
                vi_put(vi_env, r->elem0 + i, vi_array_elem(t, st->child, i, st->child), -1, -1);
                vi_slot(vi_env, r->elem0 + i)->mask = vi_mask(t->elem.w);
            }
        } else if (r->ty == T_INT) {   /* an integer: worked out while inlining (a constant), or an integer signal */
            x = vi_gen(st->child, -1);
            if (x.ty == T_INTV) { r->ty = T_INTV; r->w = x.w; r->left = x.w - 1; r->right = 0; r->down = 1; }
            else if (x.ty != T_INT) vi_fail(st->child, "this function returns an integer, but this is %s", vi_desc(&x));
            else if (!vi_live(vi_env)) vi_fail(st, "an integer function returns here only on some paths; Wire works its value "
                                                    "out while reading, so return it on every path the same way");
            vi_put(vi_env, vi_ret_si, x, -1, -1);
            vi_slot(vi_env, vi_ret_si)->mask = vi_mask(r->w ? r->w : 1);
        } else {
            x = vi_gen(st->child, r->w ? r->w : -1);
            if (!r->w) {   /* std_logic_vector, unsigned ...: as wide as what it returns */
                if (x.ty == T_INT || x.w < 1) vi_fail(st->child, "Wire cannot tell how wide this is; give the function's return type a range");
                r->w = x.w; r->left = x.w - 1; r->right = 0; r->down = 1;
            }
            memset(&tt, 0, sizeof tt);
            tt.s = "the return value"; tt.ty = r->ty; tt.w = r->w; tt.ut = r->ut;
            if (r->ty == T_INT && x.ty == T_INT) r->ival = x.iv;
            vi_assignable(&tt, &x, st->child);
            vi_put(vi_env, vi_ret_si, x, -1, -1);
            vi_slot(vi_env, vi_ret_si)->mask = vi_mask(r->w ? r->w : 1);   /* the paths not live here returned earlier */
        }
    }
    {   /* the paths live here have returned */
        VX gone = vi_not(was);
        gone.ty = T_BOOL;
        vi_env->ret = vi_and1(vi_retc(vi_env), gone);
        if (vi_env->ret.cst && (vi_env->ret.v & 1)) vi_env->ret.s = NULL;
    }
    vi_env->live = vi_const(0, 1, T_BOOL);   /* nothing after it runs */
}

static void vi_proc_call(gram_node *st);

static void vi_seq(gram_node *first)
{
    gram_node *st;
    for (st = first; st; st = st->next) {
        /* after a return or exit that is always taken, nothing runs (and a
         * function calling itself stops there) */
        if (vi_env && vi_env->live.cst && !(vi_env->live.v & 1)) break;
        vi_epoch++;
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
        case VHDL_R_pcall: vi_proc_call(st); break;
        case VHDL_R_nullst: case VHDL_R_assert: case VHDL_R_reportst: break;
        case VHDL_R_ident: break;   /* end function name; */
        case VHDL_R_waitst: vi_fail(st, "wait is for simulation; a process for hardware has a sensitivity list instead"); break;
        default: vi_fail(st, "this kind of statement is not supported yet in a process");
        }
    }
}

/* ---- functions and procedures: inlined where they are called ------------ */

/* a parameter: its name, type (open: a vector whose width comes from the
 * argument), mode (0 in, 1 out, 2 inout), class (signal: given values with <=)
 * and default */
typedef struct { gram_node *id; VSym proto; int open, mode, sig; gram_node *def, *type; } VParam;

static int vi_unconstrained(gram_node *t, VSym *s);

static int vi_params(gram_node *f, VParam *ps, int max)
{
    gram_node *name = f->child, *fp = name->next && name->next->rule == VHDL_R_fparams ? name->next : NULL, *p, *pid;
    int n = 0;
    for (p = fp ? fp->child : NULL; p; p = p->next) {
        gram_node *t = vi_kid(p, VHDL_R_type), *mode = vi_kid(p, VHDL_R_mode), *cls = vi_kid(p, VHDL_R_fclass);
        VParam q;
        memset(&q, 0, sizeof q);
        q.type = t;
        q.def = t->next;
        q.mode = !mode || vi_is(mode, "in") ? 0 : vi_is(mode, "out") || vi_is(mode, "buffer") ? 1 : 2;
        q.sig = cls && vi_is(cls, "signal");
        if (f->rule == VHDL_R_funcdecl && q.mode) vi_fail(mode, "a function's parameters are inputs; use a procedure for outputs");
        if (!cls && q.mode && !q.sig) q.sig = 0;   /* a variable, as VHDL has it for out and inout */
        for (pid = vi_kid(p, VHDL_R_idlist)->child; pid; pid = pid->next) {
            if (n >= max) vi_fail(p, "more than %d parameters", max);
            q.id = pid;
            ps[n++] = q;
        }
    }
    return n;
}

/* the type of a parameter, read where it is called */
static void vi_param_type(VParam *q)
{
    memset(&q->proto, 0, sizeof q->proto);
    q->open = vi_unconstrained(q->type, &q->proto);
    if (!q->open) {
        VType *ut = vi_find_type(q->type->child, NULL);
        if (ut && ut->isarr && ut->unbound && !q->type->child->next) {   /* an array type without a range: the argument's */
            q->open = 2;
            q->proto.ty = T_ARR;
            q->proto.ut = (int)(ut - vi_types) + 1;
            return;
        }
        vi_type(q->type, &q->proto, 1);
    }
}

/* which argument goes to each parameter (NULL: its default) */
static int vi_assoc(gram_node *args, VParam *ps, int np, gram_node **act, int fail, const gram_node *at)
{
    gram_node *a;
    int pos = 0, named = 0, i;
    for (i = 0; i < np; i++) act[i] = NULL;
    for (a = args ? args->child : NULL; a; a = a->next) {
        if (a->rule == VHDL_R_named) {
            named = 1;
            for (i = 0; i < np; i++) if (ps[i].id->len == a->child->len && vi_is(a->child, vi_fmt("%.*s", VT(ps[i].id)))) break;
            if (i == np) { if (fail) vi_fail(a->child, "there is no parameter called %.*s", VT(a->child)); return 0; }
            if (act[i]) { if (fail) vi_fail(a, "%.*s is given twice", VT(a->child)); return 0; }
            act[i] = a->child->next;
            continue;
        }
        if (named) { if (fail) vi_fail(a, "put the arguments without a name before the named ones"); return 0; }
        if (pos >= np) { if (fail) vi_fail(a, "too many arguments: it has %d parameters", np); return 0; }
        act[pos++] = a;
    }
    for (i = 0; i < np; i++)
        if (!act[i] && !ps[i].def) {
            if (fail) vi_fail(at, "parameter %.*s is given no value (and has no default)", VT(ps[i].id));
            return 0;
        }
    return 1;
}

/* how well an argument fits a parameter's type: 0 not at all, 1 a literal that would do, 2 exactly */
static int vi_fits(const VParam *q, gram_node *a)
{
    VX x;
    if (q->proto.ty == T_ARR || q->proto.ty == T_REC) {
        VSym *s = a->rule == VHDL_R_ident ? vi_find(a) : NULL;
        if (!s || s->ty != q->proto.ty) return 0;
        if (q->proto.ty == T_REC) return s->ut == q->proto.ut ? 2 : 0;
        return 2;
    }
    x = vi_gen_any(a, -1);
    switch (q->proto.ty) {
    case T_INT: case T_INTV: return x.ty == T_INT || x.ty == T_INTV ? 2 : 0;
    case T_BOOL: return x.ty == T_BOOL ? 2 : 0;
    case T_SL: return x.ty == T_SL ? 2 : x.ty == T_STR && x.w == 1 && a->rule == VHDL_R_chr ? 1 : 0;
    case T_ENUM: return x.ty == T_ENUM && x.tid == q->proto.ut ? 2 : 0;
    default:
        if (!vi_vec(x.ty) || (x.ty == T_STR && a->rule == VHDL_R_chr)) return 0;
        if (x.ty == T_STR) return 1;
        if (x.ty == q->proto.ty) return 2;
        return 0;
    }
}

static void vi_set_ctx(int ctx);

static int vi_func_ctx(const gram_node *f)   /* the use clauses f's body is read with */
{
    int i;
    for (i = 0; i < vi_nfuncs; i++) if (vi_funcs[i] == f) return vi_fctx[i];
    return vi_ctx;
}

static int vi_is_func(const gram_node *f, int proc)
{
    return proc ? f->rule == VHDL_R_procdecl : f->rule == VHDL_R_funcdecl;
}

/* the function (or procedure) called id that these arguments fit best; NULL if none */
static gram_node *vi_find_sub(const gram_node *id, gram_node *args, int proc)
{
    int i, best = -1, nbest = 0, nnamed = 0;
    gram_node *bf = NULL;
    for (i = vi_nfuncs - 1; i >= 0; i--) {
        gram_node *f = vi_funcs[i], *name = f->child, *act[64];
        VParam ps[64];
        int np, k, score = 0;
        if (!vi_is_func(f, proc)) continue;
        if (name->len != id->len || !vi_is(id, vi_fmt("%.*s", VT(name)))) continue;
        nnamed++;
        np = vi_params(f, ps, 64);
        if (!vi_assoc(args, ps, np, act, 0, id)) continue;
        for (k = 0; k < np && score >= 0; k++) {
            int fit;
            if (!act[k]) continue;
            vi_param_type(&ps[k]);
            if (ps[k].mode == 1) {   /* an out: what it is given to is not read; its declared type says */
                gram_node *aid = act[k]->rule == VHDL_R_name ? act[k]->child : act[k];
                VSym *as = aid->rule == VHDL_R_ident ? vi_find(aid) : NULL;
                int pt = ps[k].proto.ty;
                if (!as) fit = 0;
                else if (aid != act[k]) fit = vi_vec(as->ty) || as->ty == T_ARR ? 1 : 0;   /* a bit or slice of it */
                else if (pt == as->ty) fit = 2;
                else fit = (pt == T_INT && as->ty == T_INTV) ? 2 : 0;
            } else fit = vi_fits(&ps[k], act[k]);
            if (!fit) score = -1; else score += fit;
        }
        if (score < 0) continue;
        if (score > best) { best = score; bf = f; nbest = 1; }
        else if (score == best) nbest++;
    }
    if (bf && nbest > 1) vi_fail(id, "%.*s(...) fits more than one %s of that name; convert the arguments, like unsigned(x)",
                                 VT(id), proc ? "procedure" : "function");
    if (!bf && nnamed && proc) vi_fail(id, "these arguments do not fit any procedure called %.*s", VT(id));
    return bf;
}

/* the function called id: by its arguments when there are several */
static gram_node *vi_find_func(const gram_node *id, int nargs)
{
    int i;
    (void)nargs;
    for (i = vi_nfuncs - 1; i >= 0; i--) {
        gram_node *f = vi_funcs[i], *name = f->child;
        if (f->rule == VHDL_R_funcdecl && name->len == id->len && vi_is(id, vi_fmt("%.*s", VT(name)))) return f;
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
#define VI_MAXDEPTH 64

/* a parameter given its value: a variable (or constant) of the body, from the argument */
static VX *vi_apre;   /* an array argument's elements, read where it is called */

static VSym *vi_bind_param(VParam *q, gram_node *a, VX *pre, int havepre)
{
    VSym *s;
    int si, i;
    if (q->proto.ty == T_ARR) {   /* an array: each element */
        VSym *as = a && a->rule == VHDL_R_ident ? vi_find(a) : NULL;
        VType *t;
        int n;
        if (!as || as->ty != T_ARR) vi_fail(a ? a : q->id, "%.*s is an array: give it an array", VT(q->id));
        if (q->open == 2) q->proto.ut = as->ut;   /* the argument's range */
        t = &vi_types[q->proto.ut - 1];
        n = vi_nelems(t);
        if (vi_nelems(&vi_types[as->ut - 1]) != n || vi_types[as->ut - 1].elem.w != t->elem.w)
            vi_fail(a, "this array does not fit parameter %.*s", VT(q->id));
        s = vi_declare(q->id, VK_VAR);
        si = (int)(s - vi_syms);
        vi_syms[si].ty = T_ARR; vi_syms[si].ut = q->proto.ut; vi_syms[si].w = t->elem.w;
        vi_syms[si].elem0 = vi_nsyms;
        for (i = 0; i < n; i++) {
            VSym *e;
            if (vi_nsyms == vi_cap) { vi_cap = vi_cap ? vi_cap * 2 : 64; vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms); }
            e = &vi_syms[vi_nsyms++];
            *e = t->elem;
            e->kind = VK_VAR; e->name = vi_syms[si].name; e->nlen = -1; e->wname = vi_fmt("%s_%d", vi_syms[si].wname, i);
            e->at = q->id;
        }
        if (q->mode != 1)
            for (i = 0; i < n; i++) vi_put(vi_env, vi_syms[si].elem0 + i, vi_apre ? vi_apre[i] : vi_elem_val(as, i, a), -1, -1);
        return &vi_syms[si];
    }
    if (q->proto.ty == T_INT && q->mode == 0) {   /* an integer constant */
        VX x = havepre ? *pre : vi_gen(a ? a : q->def, -1);
        if (x.ty == T_INTV) {   /* an integer signal: a variable as wide as it */
            s = vi_declare(q->id, VK_VAR);
            s->ty = T_INTV; s->w = x.w; s->left = x.w - 1; s->right = 0; s->down = 1;
            vi_put(vi_env, (int)(s - vi_syms), x, -1, -1);
            return s;
        }
        if (!x.cst || x.ty != T_INT) vi_fail(a ? a : q->def, "an integer argument must be a constant or an integer signal");
        s = vi_declare(q->id, VK_CONST);
        s->ty = T_INT; s->ival = x.iv;
        return s;
    }
    {
        VX x;
        int w = q->proto.w;
        memset(&x, 0, sizeof x);
        if (q->mode != 1 || havepre) {
            x = havepre ? *pre : vi_gen(a ? a : q->def, q->open ? -1 : w);
            if (q->open) {
                if (x.ty == T_INT || x.w < 1 || x.ty == T_SL) vi_fail(a, "this argument is a vector");
                w = x.w;
            } else if (q->proto.ty == T_INTV && x.ty == T_INT) x = vi_int_as(&x, w, T_INTV, a ? a : q->def);
            else {
                VTarget tt;
                memset(&tt, 0, sizeof tt);
                tt.s = vi_fmt("%.*s", VT(q->id)); tt.ty = q->proto.ty; tt.w = w; tt.ut = q->proto.ut;
                vi_assignable(&tt, &x, a ? a : q->def);
            }
        }
        s = vi_declare(q->id, VK_VAR);
        si = (int)(s - vi_syms);
        vi_syms[si].ty = q->proto.ty == T_STR ? T_SLV : q->proto.ty; vi_syms[si].w = w; vi_syms[si].ut = q->proto.ut;
        vi_syms[si].left = q->open ? w - 1 : q->proto.left; vi_syms[si].right = q->open ? 0 : q->proto.right;
        vi_syms[si].down = q->open ? 1 : q->proto.down;
        vi_syms[si].intneg = q->proto.intneg;
        vi_syms[si].sigparam = q->sig;
        if (q->open && a && a->rule == VHDL_R_ident) {   /* the argument's own range */
            VSym *as = vi_find(a);
            if (as && as->w == w && vi_vec(as->ty)) { vi_syms[si].left = as->left; vi_syms[si].right = as->right; vi_syms[si].down = as->down; }
        }
        if (x.ty == T_STR) x.ty = vi_syms[si].ty;
        if (q->mode != 1 || havepre) vi_put(vi_env, si, x, -1, -1);
        return &vi_syms[si];
    }
}

/* a function that returns an array, called to give element i of it (each call once per statement) */
static struct { gram_node *call; long epoch; VX *v; int n; } vi_fcache;
static VX vi_func_call_arr(gram_node *f, gram_node *id, gram_node *args, VX **out, int *nout);

static VX vi_func_elem(gram_node *v, VType *t, int i)
{
    if (vi_fcache.call != v || vi_fcache.epoch != vi_epoch) {
        gram_node *id = v->rule == VHDL_R_ident ? v : v->child, *args = v->rule == VHDL_R_ident ? NULL : v->child->next;
        gram_node *f = vi_find_sub(id, args, 0);
        if (!f) vi_fail(v, "there is no function %.*s for these arguments", VT(id));
        vi_func_call_arr(f, id, args, &vi_fcache.v, &vi_fcache.n);
        vi_fcache.call = v;
        vi_fcache.epoch = vi_epoch;
    }
    if (vi_fcache.n != vi_nelems(t)) vi_fail(v, "this function returns an array of %d elements, but %d are wanted", vi_fcache.n, vi_nelems(t));
    return vi_fcache.v[i];
}

static VX vi_func_call_arr(gram_node *f, gram_node *id, gram_node *args, VX **aout, int *naout)
{
    gram_node *name = f->child, *k = name->next, *rty, *pd, *body, *act[64];
    VParam ps[64];
    VX res, pre[64];
    int np, i, mark = vi_nsyms, tmark = vi_ntypes, fmark = vi_nfuncs, scope = vi_scope;
    int clocked = vi_clocked, loops = vi_loops, ret = vi_ret_si, rsi;
    const char *bind_at = vi_bind_at;
    VEnv env, *outer = vi_env;
    VSlot *sl;
    if (k && k->rule == VHDL_R_fparams) k = k->next;
    rty = k; pd = rty->next; body = pd->next;
    if (vi_fdepth >= VI_MAXDEPTH)
        vi_fail(id, "functions call each other more than %d deep (a function that calls itself must stop on a constant, "
                    "like a generic)", VI_MAXDEPTH);
    np = vi_params(f, ps, 64);
    vi_assoc(args, ps, np, act, 1, id);
    /* the arguments, worked out where the function is called (arrays: in the body, from the argument's elements) */
    VX *apre[64];
    for (i = 0; i < np; i++) {
        vi_param_type(&ps[i]);
        apre[i] = NULL;
        if (ps[i].proto.ty != T_ARR)
            pre[i] = vi_gen(act[i] ? act[i] : ps[i].def, ps[i].proto.ty == T_INT || ps[i].open ? -1 : ps[i].proto.w);
        else {
            VSym *as = act[i] && act[i]->rule == VHDL_R_ident ? vi_find(act[i]) : NULL;
            int e, ne;
            if (!as || as->ty != T_ARR) vi_fail(act[i] ? act[i] : id, "%.*s is an array: give it an array", VT(ps[i].id));
            ne = vi_nelems(&vi_types[as->ut - 1]);
            apre[i] = (VX *)malloc((size_t)ne * sizeof *apre[i]);
            for (e = 0; e < ne; e++) apre[i][e] = vi_elem_val(as, e, act[i]);
        }
    }
    /* the body, in a scope and a set of values of its own */
    memset(&env, 0, sizeof env);
    env.live = vi_const(1, 1, T_BOOL);
    vi_scope = vi_nsyms;
    vi_env = &env; vi_clocked = 0; vi_loops = 0;
    for (i = 0; i < np; i++) { vi_apre = apre[i]; vi_bind_param(&ps[i], act[i], &pre[i], ps[i].proto.ty != T_ARR); }
    vi_apre = NULL;
    {
        VSym proto, *r;
        memset(&proto, 0, sizeof proto);
        if (!vi_unconstrained(rty, &proto)) {
            VType *ut = vi_find_type(rty->child, NULL);
            if (ut && ut->isarr && ut->unbound) vi_fail(rty, "give the array this function returns a range (a subtype with one)");
            vi_type(rty, &proto, rty->child->next ? 2 : 1);   /* integer: a constant; integer range a to b: bits */
        }
        r = vi_declare(name, VK_VAR);
        rsi = (int)(r - vi_syms);
        vi_syms[rsi].ty = proto.ty; vi_syms[rsi].w = proto.w; vi_syms[rsi].left = proto.left; vi_syms[rsi].right = proto.right;
        vi_syms[rsi].down = proto.down; vi_syms[rsi].ut = proto.ut; vi_syms[rsi].intneg = proto.intneg;
        vi_syms[rsi].nlen = 0;   /* not found by name: a call of the function is not its value */
        if (proto.ty == T_ARR) {   /* returns an array: a variable per element */
            VType *t = &vi_types[proto.ut - 1];
            int n = vi_nelems(t);
            vi_syms[rsi].elem0 = vi_nsyms;
            for (i = 0; i < n; i++) {
                VSym *e;
                if (vi_nsyms == vi_cap) { vi_cap = vi_cap ? vi_cap * 2 : 64; vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms); }
                e = &vi_syms[vi_nsyms++];
                *e = t->elem;
                e->kind = VK_VAR; e->name = vi_syms[rsi].name; e->nlen = -1; e->wname = vi_fmt("%s_r%d", vi_syms[rsi].wname, i);
                e->at = name;
            }
        } else if (proto.ty == T_INT) {   /* an integer result: a constant, worked out while inlining */
            vi_syms[rsi].ty = T_INT;
        }
    }
    vi_ret_si = rsi;
    if (pd->child) vi_decls(pd->child);
    for (i = rsi + 1; i < vi_nsyms; i++)   /* variables that start with a value */
        if (vi_syms[i].kind == VK_VAR && vi_syms[i].hasinit) vi_put(&env, i, vi_const(vi_syms[i].bits, vi_syms[i].w, vi_syms[i].ty), -1, -1);
    if (!bind_at) vi_bind_at = id->text;
    vi_fdepth++;
    {
        int cctx = vi_ctx;
        vi_set_ctx(vi_func_ctx(f));
        vi_seq(body && body->rule != VHDL_R_ident ? body : NULL);
        vi_set_ctx(cctx);
    }
    vi_fdepth--;
    vi_bind_at = bind_at;
    if (vi_syms[rsi].ty == T_ARR) {
        VType *t = &vi_types[vi_syms[rsi].ut - 1];
        int n = vi_nelems(t);
        *aout = (VX *)malloc((size_t)n * sizeof **aout);
        *naout = n;
        for (i = 0; i < n; i++) {
            sl = vi_slot_find(&env, vi_syms[rsi].elem0 + i);
            if (!sl || sl->mask != vi_mask(t->elem.w)) vi_fail(id, "%.*s does not return a value on every path", VT(id));
            (*aout)[i] = sl->v;
            (*aout)[i].tid = t->elem.ut;
        }
        memset(&res, 0, sizeof res);
    } else {
        if (!aout && 0) {}
        sl = vi_slot_find(&env, rsi);
        if (vi_syms[rsi].ty == T_INT) {   /* an integer: the value it returned (a constant) */
            if (!sl || !sl->v.cst || sl->v.ty != T_INT) vi_fail(id, "%.*s returns an integer that Wire cannot work out while reading it", VT(id));
            res = sl->v;
        } else {
            if (!sl || !vi_syms[rsi].w || sl->mask != vi_mask(vi_syms[rsi].w))
                vi_fail(id, "%.*s does not return a value on every path", VT(id));
            res = sl->v;
            res.ty = vi_syms[rsi].ty;
            res.tid = vi_syms[rsi].ut;
        }
        if (aout) { *aout = NULL; *naout = 0; }
    }
    vi_env = outer; vi_clocked = clocked; vi_loops = loops; vi_ret_si = ret;
    vi_scope = scope;
    vi_nsyms = mark; vi_ntypes = tmark; vi_nfuncs = fmark;
    return res;
}

static VX vi_func_call(gram_node *f, gram_node *id, gram_node *args)
{
    VX *arr = NULL;
    int n = 0;
    gram_node *best = vi_find_sub(id, args, 0);
    if (best) f = best;
    {
        VX r = vi_func_call_arr(f, id, args, &arr, &n);
        if (arr) vi_fail(id, "%.*s returns an array; give it to an array signal or variable", VT(id));
        return r;
    }
}

/* name(args); inside a process: the procedure's body, run right here (in the
 * process's own values), with its parameters as variables; out and inout
 * parameters give their values back to their arguments at the end */
static void vi_proc_call(gram_node *st)
{
    gram_node *n = st->child, *id = n->rule == VHDL_R_name ? n->child : n, *args = n->rule == VHDL_R_name ? id->next : NULL;
    gram_node *f, *pd, *body, *k, *act[64];
    VParam ps[64];
    VSym *formal[64];
    int np, i, mark = vi_nsyms, tmark = vi_ntypes, fmark = vi_nfuncs, scope = vi_scope, loops = vi_loops, ret = vi_ret_si;
    VX live, rt;
    if (args && (args->rule != VHDL_R_args || args->next)) vi_fail(n, "this is not a procedure call");
    if (!vi_env) vi_fail(st, "a procedure is called inside a process (or on its own, as a statement)");
    f = vi_find_sub(id, args, 1);
    if (!f) {
        if (vi_find(id)) vi_fail(st, "%.*s is not a procedure; a statement gives a value with <= or :=", VT(id));
        vi_fail(id, "there is no procedure called %.*s", VT(id));
    }
    if (vi_fdepth >= VI_MAXDEPTH)
        vi_fail(id, "procedures call each other more than %d deep (one that calls itself must stop on a constant)", VI_MAXDEPTH);
    np = vi_params(f, ps, 64);
    vi_assoc(args, ps, np, act, 1, id);
    for (k = f->child->next; k && k->rule != VHDL_R_pdecls; k = k->next) {}
    pd = k; body = pd->next;
    /* the inputs are worked out before any parameter exists, with the names of the caller */
    {
        VX pre[64];
        int havepre[64];
        for (i = 0; i < np; i++) {
            vi_param_type(&ps[i]);
            havepre[i] = 0;
            if (ps[i].mode && (!act[i] || act[i]->rule == VHDL_R_OPEN))
                vi_fail(act[i] ? act[i] : id, "out parameter %.*s must be given a signal or variable", VT(ps[i].id));
            if (ps[i].proto.ty != T_ARR && (ps[i].mode != 1)) {
                pre[i] = vi_gen(act[i] ? act[i] : ps[i].def, ps[i].proto.ty == T_INT || ps[i].open ? -1 : ps[i].proto.w);
                havepre[i] = 1;
            } else if (ps[i].mode == 1 && ps[i].proto.ty != T_ARR) {   /* out: its width, from what it is given to */
                memset(&pre[i], 0, sizeof pre[i]);
                if (ps[i].open) {
                    gram_node *aid = act[i]->rule == VHDL_R_name ? act[i]->child : act[i];
                    VSym *as = aid->rule == VHDL_R_ident ? vi_find(aid) : NULL;
                    int hi, lo, w;
                    if (!as) vi_fail(act[i], "out parameter %.*s must be given a signal or variable", VT(ps[i].id));
                    w = as->w;
                    if (act[i]->rule == VHDL_R_name && aid->next && aid->next->rule == VHDL_R_args) {
                        vi_bitrange(as, aid, aid->next->child, &hi, &lo);
                        w = hi - lo + 1;
                    }
                    ps[i].proto.w = w; ps[i].open = 0; ps[i].proto.left = w - 1; ps[i].proto.right = 0; ps[i].proto.down = 1;
                }
            }
        }
        vi_scope = vi_nsyms;
        for (i = 0; i < np; i++) formal[i] = vi_bind_param(&ps[i], act[i], &pre[i], havepre[i]);
    }
    if (pd && pd->child) vi_decls(pd->child);
    for (i = mark; i < vi_nsyms; i++)
        if (vi_syms[i].kind == VK_VAR && vi_syms[i].hasinit) vi_put(vi_env, i, vi_const(vi_syms[i].bits, vi_syms[i].w, vi_syms[i].ty), -1, -1);
    live = vi_env->live; rt = vi_env->ret;
    vi_env->ret.s = NULL;
    vi_ret_si = -2; vi_loops = 0;
    vi_fdepth++;
    {
        int cctx = vi_ctx;
        vi_set_ctx(vi_func_ctx(f));
        vi_seq(body && body->rule != VHDL_R_ident ? body : NULL);
        vi_set_ctx(cctx);
    }
    vi_fdepth--;
    vi_env->live = live; vi_env->ret = rt;   /* after the call, the caller goes on (a return ends only the procedure) */
    vi_ret_si = ret; vi_loops = loops;
    /* out and inout: their values to the arguments, as if assigned right here
     * (with the procedure's own names hidden, so the arguments mean the caller's) */
    for (i = mark; i < vi_nsyms; i++) if (vi_syms[i].nlen > 0) vi_syms[i].nlen = -vi_syms[i].nlen - 1;
    for (i = 0; i < np; i++) {
        VSym *fs = &vi_syms[formal[i] - vi_syms];
        if (!ps[i].mode) continue;
        if (fs->ty == T_ARR) {
            VSym *as = vi_find(act[i]);
            VType *t = &vi_types[fs->ut - 1];
            int e, ne = vi_nelems(t);
            if (!as || as->ty != T_ARR || as->kind == VK_IN || as->kind == VK_CONST)
                vi_fail(act[i], "out parameter %.*s must be given an array signal or variable", VT(ps[i].id));
            for (e = 0; e < ne; e++) {
                VSlot *sl = vi_slot_find(vi_env, fs->elem0 + e);
                if (!sl || sl->mask != vi_mask(t->elem.w)) vi_fail(id, "%.*s does not give %.*s a value on every path", VT(id), VT(ps[i].id));
                vi_put(vi_env, as->elem0 + e, sl->v, -1, -1);
            }
            continue;
        }
        {
            VSlot *sl = vi_slot_find(vi_env, (int)(fs - vi_syms));
            gram_node *tgt, *val;
            VSym *as = act[i]->rule == VHDL_R_ident ? vi_find(act[i]) : NULL;
            static char tmpname[] = "proc_out_";
            int isvar;
            if (!sl || sl->mask != vi_mask(fs->w)) {
                if (as && !as->wnch && sl && as->kind != VK_IN) {   /* given a value on some paths: it keeps its own on the others */
                    VSlot *asl = vi_slot(vi_env, (int)(as - vi_syms));
                    VX keep = asl->v;
                    unsigned long m = sl->mask;
                    if (m && asl->mask != vi_mask(as->w) && !vi_clocked && as->kind != VK_VAR)
                        vi_fail(id, "%.*s does not give %.*s a value on every path, so %.*s would need memory (a latch)",
                                VT(id), VT(ps[i].id), VT(act[i]));
                    (void)keep;
                }
                if (!sl || !sl->mask) continue;   /* never given a value: the argument keeps its own */
                vi_fail(id, "%.*s gives %.*s a value only on some paths; give it one on every path", VT(id), VT(ps[i].id));
            }
            /* the value, as a variable the argument is assigned from */
            fs->name = tmpname; fs->nlen = (int)strlen(tmpname);
            val = (gram_node *)calloc(1, sizeof *val);
            val->rule = VHDL_R_ident; val->text = tmpname; val->len = strlen(tmpname);
            tgt = (gram_node *)calloc(1, sizeof *tgt);
            tgt->rule = VHDL_R_target;
            if (act[i]->rule == VHDL_R_ident) tgt->child = vi_node_copy(act[i], NULL);
            else if (act[i]->rule == VHDL_R_name && act[i]->child->next && act[i]->child->next->rule == VHDL_R_args
                     && !act[i]->child->next->next) {
                gram_node *a, *last = NULL;
                tgt->child = vi_node_copy(act[i]->child, NULL);
                for (a = act[i]->child->next->child; a; a = a->next) {
                    gram_node *c = vi_node_copy(a, NULL);
                    if (last) last->next = c; else tgt->child->next = c;
                    last = c;
                }
            } else vi_fail(act[i], "out parameter %.*s must be given a signal or variable (or a bit or slice of one)", VT(ps[i].id));
            {
                VSym *ts = vi_find(tgt->child);
                if (!ts) vi_fail(act[i], "there is no signal or variable called %.*s", VT(act[i]));
                isvar = ts->kind == VK_VAR;
            }
            vi_seq_assign(tgt, val, isvar);
            fs->nlen = -1;
        }
    }
    /* the procedure's own names are gone: their values too */
    {
        int j, o = 0;
        for (j = 0; j < vi_env->n; j++) if (vi_env->s[j].sym < mark) vi_env->s[o++] = vi_env->s[j];
        vi_env->n = o;
    }
    vi_scope = scope;
    vi_nsyms = mark; vi_ntypes = tmark; vi_nfuncs = fmark;
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

static int vi_txteq(const gram_node *a, const gram_node *b);

/* else null; (or an else with nothing in it) */
static int vi_null_else(const gram_node *rest)
{
    const gram_node *k;
    if (!rest || rest->rule != VHDL_R_elsep) return 0;
    for (k = rest->child->child; k; k = k->next)
        if (k->rule != VHDL_R_nullst && k->rule != VHDL_R_assert && k->rule != VHDL_R_reportst) return 0;
    return 1;
}

/* if rising_edge(clk) then ... end if, or if rst = '1' then ... elsif rising_edge(clk) then ... end if */
static int vi_edge_if(gram_node *st, gram_node **clk, int *neg, gram_node **rst, gram_node **rbody, gram_node **cbody)
{
    gram_node *c, *body, *rest;
    static const char *noelse = "an else or elsif after the clock edge is not hardware (VHDL tools refuse it too): "
                                "the registers change only on the edge; give the other signals values in a process of their own";
    if (st->rule != VHDL_R_ifst) return 0;
    c = st->child;
    if (c->rule == VHDL_R_label) c = c->next;
    body = c->next;
    rest = body->next && body->next->rule != VHDL_R_ident ? body->next : NULL;
    *rst = *rbody = *cbody = NULL;
    if (vi_edge(c, clk, neg)) {
        if (rest && !vi_null_else(rest)) vi_fail(rest, "%s", noelse);
        *cbody = body;
        return 1;
    }
    if (rest && rest->rule == VHDL_R_elsifp && vi_edge(rest->child, clk, neg)) {
        if (rest->next && rest->next->rule != VHDL_R_ident && !vi_null_else(rest->next)) vi_fail(rest->next, "%s", noelse);
        *rst = c; *rbody = body; *cbody = rest->child->next;
        return 1;
    }
    return 0;
}

/* wait until rising_edge(clk) (or clk = '1', or clk'event and clk = '1') */
static int vi_wait_edge(gram_node *st, gram_node **clk, int *neg)
{
    gram_node *e;
    if (st->rule != VHDL_R_waitst || !st->child->next) return 0;
    e = st->child->next;
    if (vi_edge(e, clk, neg)) return 1;
    if (e->rule == VHDL_R_relation && e->child->rule == VHDL_R_ident && vi_is(e->child->next, "=")
        && e->child->next->next->rule == VHDL_R_chr) {   /* until clk = '1': it waits for clk to change, so an edge */
        *clk = e->child;
        *neg = e->child->next->next->text[1] == '0';
        return 1;
    }
    return 0;
}

/* the statements from a up to (not with) b, as the body of an if */
static gram_node *vi_stmts(gram_node *a, gram_node *b, const gram_node *at)
{
    gram_node *body = (gram_node *)calloc(1, sizeof *body), *last = NULL, *k;
    body->rule = VHDL_R_sbody; body->text = at->text; body->len = at->len;
    for (k = a; k && k != b; k = k->next) {
        gram_node *c = vi_node_copy(k, NULL);
        if (last) last->next = c; else body->child = c;
        last = c;
    }
    return body;
}

/* the clocked shape: 1 for one if rising_edge(clk) then ... end if (with or
 * without an asynchronous reset first) and nothing else, or a process that
 * waits for the edge (wait until rising_edge(clk)); 2 for clock edges beside
 * other statements (logic), or several, all on one clock: vi_process reads
 * those one by one */
static int vi_proc_shape(gram_node *first, gram_node **clk, int *neg, gram_node **rst, gram_node **rbody, gram_node **cbody)
{
    gram_node *k, *st = NULL, *fw = NULL, *lw = NULL, *c2, *r2, *rb2, *cb2;
    int n = 0, nw = 0, nedge = 0, n2;
    for (k = first; k; k = k->next)
        if (k->rule != VHDL_R_ident && k->rule != VHDL_R_nullst && k->rule != VHDL_R_assert && k->rule != VHDL_R_reportst) {
            if (!fw) fw = k;
            lw = k;
            st = k;
            n++;
            if (k->rule == VHDL_R_waitst) nw++;
        }
    *rst = *rbody = *cbody = NULL;
    if (n == 1 && vi_edge_if(st, clk, neg, rst, rbody, cbody)) return 1;
    if (nw == 1 && fw && vi_wait_edge(fw, clk, neg)) { *cbody = vi_stmts(fw->next, NULL, fw); return 1; }
    if (nw == 1 && lw && vi_wait_edge(lw, clk, neg)) { *cbody = vi_stmts(first, lw, lw); return 1; }
    if (nw) return 0;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_ifst && vi_edge_if(k, nedge ? &c2 : clk, nedge ? &n2 : neg, nedge ? &r2 : rst,
                                                 nedge ? &rb2 : rbody, nedge ? &cb2 : cbody)) {
            if (nedge && (!vi_txteq(c2, *clk) || n2 != *neg))
                vi_fail(k, "one process has one clock: give this clock edge a process of its own");
            if (nedge && ((r2 == NULL) != (*rst == NULL) || (r2 && !vi_txteq(r2, *rst))))
                vi_fail(k, "the clock edges of one process have the same reset (or none)");
            nedge++;
            continue;
        }
        if (vi_has_edge(k)) return 0;
    }
    return nedge ? 2 : 0;
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
    ds = d.s;
    /* a register that resets (or starts) at k, not 0: init = k, the way
     * Logisim presets the 1 bits and clears the others */
    call = vi_fmt("dff(%s, %s%s%s%s%s%s%s)", ds, clk, en ? ", en = " : "", en ? en : "", rst && has_reset ? ", rst = " : "",
                  rst && has_reset ? rst : "", k ? ", init = " : "", k ? vi_lit(k, s->w) : "");
    vi_emit(at, "%s = %s", q, call);
}

/* the reset part: each signal gets a constant */
typedef struct { int sym; unsigned long v; int z; } VReset;

static int vi_reset_vals(gram_node *first, VReset **out)
{
    gram_node *st;
    int n = 0, cap = 0;
    VReset *r = NULL;
    for (st = first; st; st = st->next) {
        gram_node *id, *a1, *v;
        VSym *s;
        int i, cnt = 1, base;
        if (st->rule == VHDL_R_nullst || st->rule == VHDL_R_assert || st->rule == VHDL_R_reportst) continue;
        if (st->rule != VHDL_R_sassign)
            vi_fail(st, "the reset part of a clocked process only gives signals constant values (<=)");
        id = st->child->child; a1 = id->next;
        v = st->child->next->rule == VHDL_R_wave ? st->child->next->child : st->child->next;
        {
            gram_node *tg = vi_unalias(st->child);
            id = tg->child; a1 = id->next;
        }
        s = vi_find(id);
        if (!s || s->kind == VK_IN || s->kind == VK_CONST || s->kind == VK_GEN || s->kind == VK_LABEL || s->kind == VK_VAR)
            vi_fail(id, "%.*s cannot be given a value here", VT(id));
        if (s->ty == T_REC && !a1) {   /* a whole record: its fields, one by one */
            VType *rt = &vi_types[s->ut - 1];
            gram_node *list = NULL, *last = NULL;
            VReset *sub;
            int f, ns, si0 = (int)(s - vi_syms);
            for (f = 0; f < rt->nf; f++) {
                gram_node *a = (gram_node *)calloc(1, sizeof *a), *tg = (gram_node *)calloc(1, sizeof *tg);
                a->rule = VHDL_R_sassign; a->text = st->text; a->len = st->len;
                tg->rule = VHDL_R_target; tg->text = st->child->text; tg->len = st->child->len;
                tg->child = vi_ident_of(vi_syms[vi_syms[si0].fld0 + f].name, NULL);
                tg->next = vi_node_copy(vi_rec_part(v, rt, f), NULL);
                a->child = tg;
                if (last) last->next = a; else list = a;
                last = a;
            }
            ns = vi_reset_vals(list, &sub);
            for (f = 0; f < ns; f++) {
                if (n == cap) { cap = cap ? cap * 2 : 16; r = (VReset *)realloc(r, (size_t)cap * sizeof *r); }
                r[n++] = sub[f];
            }
            continue;
        }
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
        if (s->wnch) {   /* wider than 32 bits: each of its registers */
            VX x;
            int c;
            vi_zok = s->zd != NULL || vi_syms[s->wch0].zd != NULL;
            x = vi_gen_any(v, s->w);
            vi_zok = 0;
            if (!x.z && !x.chv) vi_fail(v, "a reset value must be a constant");
            for (c = 0; c < s->wnch; c++) {
                if (n == cap) { cap = cap ? cap * 2 : 16; r = (VReset *)realloc(r, (size_t)cap * sizeof *r); }
                r[n].sym = s->wch0 + c; r[n].z = x.z; r[n++].v = x.z ? 0 : x.chv[c];
            }
            continue;
        }
        {
            VTarget tt;
            VX x;
            memset(&tt, 0, sizeof tt);
            vi_zok = s->zd != NULL;
            x = vi_gen(v, s->w);
            vi_zok = 0;
            tt.s = s->wname; tt.ty = s->ty; tt.w = s->w; tt.ut = s->ut;
            vi_assignable(&tt, &x, v);
            if (!x.cst && !x.z) vi_fail(v, "a reset value must be a constant");
            if (n == cap) { cap = cap ? cap * 2 : 16; r = (VReset *)realloc(r, (size_t)cap * sizeof *r); }
            r[n].sym = base; r[n].z = x.z; r[n++].v = x.z ? 0 : x.v;
        }
    }
    *out = r;
    return n;
}


/* ---- a big process as a chip ------------------------------------------------------
 * A process that makes many parts is drawn as one chip, named after its label
 * (or process_12, its line), with the signals it reads as inputs and the
 * signals it gives values to as outputs: the circuit around it stays
 * readable, and the chip opens to show its logic. Its component is written
 * after everything else, each statement on the line it came from. */

#define VI_CHIP_MIN 12           /* parts a process makes before it becomes a chip */
static VBuf vi_pcomp;            /* the components of those processes */
static int *vi_pxline, vi_npx;   /* the VHDL line of each of their lines */
static char **vi_pnames;         /* their names */
static int vi_npnames;

static void vi_pemit(const char *at, const char *text)
{
    size_t line = 1, col = 1;
    gram_locate(vi_src, vi_len, (size_t)(at - vi_src), &line, &col);
    vb_add(&vi_pcomp, "\n", 1);
    vb_add(&vi_pcomp, text, strlen(text));
    vi_pxline = (int *)realloc(vi_pxline, (size_t)(vi_npx + 1) * sizeof *vi_pxline);
    vi_pxline[vi_npx++] = (int)line;
}

static int vi_idc(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static VSym *vi_by_wname(const char *w, int n)
{
    int i;
    for (i = vi_nsyms - 1; i >= 0; i--)
        if (vi_syms[i].wname && (int)strlen(vi_syms[i].wname) == n && !memcmp(vi_syms[i].wname, w, (size_t)n)) return &vi_syms[i];
    return NULL;
}

typedef struct { const char *s; int n, w, vec; } VPin;

static int vi_pin_has(const VPin *p, int np, const char *s, int n)
{
    int i;
    for (i = 0; i < np; i++) if (p[i].n == n && !memcmp(p[i].s, s, (size_t)n)) return 1;
    return 0;
}

static void vi_proc_out(gram_node *st, const char *at)
{
    VPin ins[MAXIO], outs[MAXIO], loc[512];
    int nin = 0, nout = 0, nloc = 0, parts = 0, i, ok = 1;
    for (i = 0; i < vi_ncaps && ok; i++) {   /* what it makes: wires of its own, and what it gives values to */
        const char *t = vi_caps[i].s, *e;
        if (!strncmp(t, "wire ", 5)) {
            t += 5;
            for (e = t; vi_idc(*e); e++) {}
            if (nloc < 512) { loc[nloc].s = t; loc[nloc++].n = (int)(e - t); } else ok = 0;
            continue;
        }
        for (e = t; vi_idc(*e); e++) {}
        if (*e == '[') { ok = 0; break; }   /* only some of its bits: leave it where it is */
        if (e == t || strncmp(e, " = ", 3)) { ok = 0; break; }
        if (vi_pin_has(loc, nloc, t, (int)(e - t))) continue;
        if (!vi_pin_has(outs, nout, t, (int)(e - t))) {
            VSym *s = vi_by_wname(t, (int)(e - t));
            if (!s || nout == MAXIO) { ok = 0; break; }
            outs[nout].s = t; outs[nout].n = (int)(e - t); outs[nout].w = s->w; outs[nout++].vec = vi_vec(s->ty) || s->w > 1;
        }
    }
    for (i = 0; i < vi_ncaps && ok; i++) {   /* what it reads */
        const char *t = vi_caps[i].s, *p;
        if (!strncmp(t, "wire ", 5)) continue;
        t = strstr(t, " = ") + 3;
        for (p = t; *p && ok; ) {
            const char *b, *q;
            if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; continue; }
            if (*p >= '0' && *p <= '9') { while (vi_idc(*p)) p++; continue; }
            if (*p == '(' || *p == '&' || *p == '|' || *p == '^' || *p == '~' || *p == '{') {
                if (*p != '(') parts++;
                p++;
                continue;
            }
            if (!vi_idc(*p)) { p++; continue; }
            b = p;
            while (vi_idc(*p)) p++;
            for (q = p; *q == ' '; q++) {}
            if (b > vi_caps[i].s && b[-1] == '.') continue;          /* a port: c.cout */
            if (*q == '(') { parts++; continue; }                     /* a part: mux(...) */
            if (*q == '=' && q[1] != '=') continue;                   /* a setting: sel = */
            if (vi_pin_has(outs, nout, b, (int)(p - b)) || vi_pin_has(loc, nloc, b, (int)(p - b)) ||
                vi_pin_has(ins, nin, b, (int)(p - b))) continue;
            {
                VSym *s = vi_by_wname(b, (int)(p - b));
                if (!s || nin == MAXIO - 2 || s->kind == VK_LABEL) { ok = 0; break; }
                ins[nin].s = b; ins[nin].n = (int)(p - b); ins[nin].w = s->w; ins[nin++].vec = vi_vec(s->ty) || s->w > 1;
            }
        }
    }
    for (i = 0; i < nin; i++) if (ins[i].n >= NAMEW) ok = 0;
    for (i = 0; i < nout; i++) if (outs[i].n >= NAMEW) ok = 0;
    if (ok) {   /* only beside other processes or chips: a process that is all its entity has is that chip already */
        gram_node *k;
        int others = 0;
        for (k = vi_cur->arch->child->next->next; k; k = k->next)
            if (k != st && (k->rule == VHDL_R_process || k->rule == VHDL_R_inst || k->rule == VHDL_R_generate)) others++;
        if (!others) ok = 0;
    }
    if (nin > 24 || nout > 16) ok = 0;   /* a chip with that many pins would not help */
    if (!ok || parts < VI_CHIP_MIN || !nout) {   /* small: its parts where the process was */
        for (i = 0; i < vi_ncaps; i++) vi_emitp(vi_caps[i].at, "%s", vi_caps[i].s);
        return;
    }
    {
        gram_node *lab = vi_kid(st, VHDL_R_label);
        size_t line = 1, col = 1;
        char *name, *inst;
        VBuf h = { 0, 0, 0 }, call = { 0, 0, 0 };
        gram_locate(vi_src, vi_len, (size_t)(at - vi_src), &line, &col);
        name = lab ? vi_fmt("%.*s%s", VT(lab->child), vi_suffix) : vi_fmt("process_%lu%s", (unsigned long)line, vi_suffix);
        for (;;) {
            int j, taken = vi_wname_taken(name) || vi_wname_used(name);
            for (j = 0; j < vi_npnames; j++) if (!strcmp(vi_pnames[j], name)) taken = 1;
            if (!taken) break;
            name = vi_fmt("%s_", name);
        }
        vi_pnames = (char **)realloc(vi_pnames, (size_t)(vi_npnames + 1) * sizeof *vi_pnames);
        vi_pnames[vi_npnames++] = name;
        inst = name;   /* the chip and its one use have one name */
        vbf(&h, "component %s(", name);
        for (i = 0; i < nin; i++) {
            vbf(&h, "%s%.*s", i ? ", " : "", ins[i].n, ins[i].s);
            if (ins[i].vec) vbf(&h, "[%d]", ins[i].w);
        }
        vb_add(&h, ") -> (", 6);
        for (i = 0; i < nout; i++) {
            vbf(&h, "%s%.*s", i ? ", " : "", outs[i].n, outs[i].s);
            if (outs[i].vec) vbf(&h, "[%d]", outs[i].w);
        }
        vb_add(&h, ") {", 3);
        vi_pemit(at, h.s);
        for (i = 0; i < vi_ncaps; i++) vi_pemit(vi_caps[i].at, vi_caps[i].s);
        vi_pemit(st->text + st->len - 1, "}");
        vbf(&call, "%s = %s(", inst, name);
        for (i = 0; i < nin; i++) vbf(&call, "%s%.*s = %.*s", i ? ", " : "", ins[i].n, ins[i].s, ins[i].n, ins[i].s);
        vb_add(&call, ")", 1);
        vi_emitp(at, "%s", call.s);
        for (i = 0; i < nout; i++) vi_emitp(at, "%.*s = %s.%.*s", outs[i].n, outs[i].s, inst, outs[i].n, outs[i].s);
    }
}

/* does n hold a 'Z' literal ('Z', "ZZZZ", (others => 'Z'))? */
static int vi_zlit(const gram_node *n)
{
    const gram_node *k;
    if (n->rule == VHDL_R_chr) return vi_lower(n->text[1]) == 'z';
    if (n->rule == VHDL_R_str) {
        size_t i;
        for (i = 1; i + 1 < n->len; i++) if (vi_lower(n->text[i]) == 'z') return 1;
        return 0;
    }
    for (k = n->child; k; k = k->next) if (vi_zlit(k)) return 1;
    return 0;
}

/* the signals a clocked process gives 'Z': each becomes a register for its
 * value and one for whether it is driven, and is tri(value, driven) */
static void vi_zregs(gram_node *first, const char *at)
{
    gram_node *k;
    for (k = first; k; k = k->next) {
        if (k->rule == VHDL_R_sassign && vi_zlit(k->child->next)) {
            VSym *s = vi_find(k->child->child);
            int c, n;
            if (!s || s->kind == VK_VAR || s->kind == VK_IN || s->ty == T_ARR || s->zd || (s->wnch && vi_syms[s->wch0].zd)) continue;
            n = s->wnch ? s->wnch : 1;
            for (c = 0; c < n; c++) {
                VSym *r = s->wnch ? &vi_syms[s->wch0 + c] : s;
                char *d = vi_fmt("%s_d", r->wname), *e = vi_fmt("%s_oe", r->wname);
                while (vi_wname_used(d)) d = vi_fmt("%s_", d);
                r->zd = d;
                while (vi_wname_used(e)) e = vi_fmt("%s_", e);
                r->ze = e;
                vi_emitp(at, "wire %s[%d], %s", d, r->w, e);
                vi_emitp(at, "%s = tri(%s, %s)", r->wname, d, e);
            }
        } else if (k->rule != VHDL_R_sassign && k->rule != VHDL_R_vassign && k->child) vi_zregs(k->child, at);
    }
}

/* the registers of a clocked process, from the values env gives them */
static void vi_emit_regs(const char *atext, gram_node *at, VEnv *envp, gram_node *clk, int neg, gram_node *rst, gram_node *rbody)
{
    VEnv env = *envp;
    VSym *cs = vi_find(clk);
    VReset *rv = NULL;
    int nr = 0, pass, i;
    char *clks, *rsts = NULL;
    (void)atext;
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
            int si = pass ? rv[i].sym : env.s[i].sym, j, has = 0, kz = 0;
            unsigned long k = 0;
            VSym *s = &vi_syms[si];
            VSlot *sl;
            VX next;
            if (s->kind == VK_VAR) continue;
            if (pass && vi_slot_find(&env, si)) continue;   /* done in the first pass */
            for (j = 0; j < nr; j++) if (rv[j].sym == si) { has = 1; k = rv[j].v; kz = rv[j].z; }
            if (!has && s->hasinit) k = s->bits;
            sl = vi_slot_find(&env, si);
            if (sl) next = sl->v;
            else { next = vi_mk(s->wname, s->ty, s->w, 2); }
            {
                VX zz;
                memset(&zz, 0, sizeof zz);
                zz.zen = s->zd ? s->ze : NULL;
                vi_driver(s, &zz, at);
            }
            if (s->zd) {   /* given 'Z': a register for the value, one for whether it is driven */
                VSym ds = *s, es = *s;
                VX data, en;
                if (!sl) { next = vi_mk(s->zd, s->ty, s->w, 2); next.zen = s->ze; }
                if (next.zmix) vi_fail(at, "some bits of %s are 'Z' and some not; give the 'Z' bits and the others "
                                           "to slices of the signal in two processes", s->wname);
                data = next.z ? vi_mk(s->zd, s->ty, s->w, 2) : vi_zdata(next);
                en = vi_zen_of(&next);
                ds.wname = s->zd;
                es.wname = s->ze; es.w = 1; es.ty = T_SL; es.ut = 0;
                vi_emit_reg(at, &ds, data, clks, rsts, has, kz ? 0 : k);
                vi_emit_reg(at, &es, en, clks, rsts, has, has ? !kz : s->hasinit);
                continue;
            }
            vi_emit_reg(at, s, next, clks, rsts, has, k);
        }
    }
}

/* the logic of a process: every signal must get a value on every path */
static void vi_emit_logic(gram_node *at, VEnv *env)
{
    int i;
    for (i = 0; i < env->n; i++) {
        VSym *s = &vi_syms[env->s[i].sym];
        if (s->kind == VK_VAR) continue;
        vi_driver(s, &env->s[i].v, at);
        if (env->s[i].mask != vi_mask(s->w))
            vi_fail(at, "%s is not given a value on every path through this process, so it would need memory (a latch); "
                        "give it a value at the top of the process", s->wname);
        vi_emit(at, "%s = %s", s->wname, vi_zs(&env->s[i].v, at));
    }
}

static void vi_process(gram_node *st)
{
    gram_node *decls, *first = vi_proc_body(st, &decls), *clk = NULL, *rst = NULL, *rbody = NULL, *cbody = NULL, *at;
    int mark, tmark, fmark, neg = 0, clocked;
    VEnv env;
    if (st->rule == VHDL_R_cpcall) {   /* a procedure called on its own: a process with only that call */
        gram_node *c = (gram_node *)calloc(1, sizeof *c), *nm = st->child;
        if (nm->rule == VHDL_R_label) nm = nm->next;
        c->rule = VHDL_R_pcall; c->text = st->text; c->len = st->len;
        c->child = vi_node_copy(nm, NULL);
        first = c;
        decls = NULL;
    }
    at = vi_kid(st, VHDL_R_PROCESS) ? vi_kid(st, VHDL_R_PROCESS) : st;
    mark = vi_nsyms; tmark = vi_ntypes; fmark = vi_nfuncs;
    memset(&env, 0, sizeof env);
    env.live = vi_const(1, 1, T_BOOL);
    if (decls) vi_decls(decls->child);
    vi_ncaps = 0;
    vi_capturing = 1;
    clocked = vi_proc_shape(first, &clk, &neg, &rst, &rbody, &cbody);
    if (!clocked) {
        gram_node *k;
        for (k = first; k; k = k->next) {
            if (k->rule == VHDL_R_waitst && k->child->next)
                vi_fail(k, "Wire reads wait until rising_edge(clk); as the first (or last) statement of a process, and only one wait");
            if (vi_has_edge(k))
                vi_fail(k, "Wire reads a clock edge as  if rising_edge(clk) then ... end if;  (or  if rst = '1' then ... elsif "
                           "rising_edge(clk) then ... end if;),  each on its own in the process or beside logic");
        }
    }
    if (clocked == 2) {   /* clock edges beside logic: the edges' signals are registers, the others logic */
        VEnv logic, regs;
        gram_node *k, *c2, *r2, *rb2, *cb2, *rlist = NULL, *rlast = NULL;
        int n2, i, j;
        memset(&logic, 0, sizeof logic);
        memset(&regs, 0, sizeof regs);
        logic.live = regs.live = vi_const(1, 1, T_BOOL);
        vi_bind_at = at->text;
        for (k = first; k; k = k->next) {
            if (k->rule == VHDL_R_ident) continue;
            if (k->rule == VHDL_R_ifst && vi_edge_if(k, &c2, &n2, &r2, &rb2, &cb2)) {
                vi_zregs(cb2->child, at->text);
                if (rb2) {   /* the reset parts, all together */
                    gram_node *q;
                    vi_zregs(rb2->child, at->text);
                    for (q = rb2->child; q; q = q->next) {
                        gram_node *cq = vi_node_copy(q, NULL);
                        if (rlast) rlast->next = cq; else rlist = cq;
                        rlast = cq;
                    }
                }
                /* the variables, as the statements before left them */
                for (i = 0; i < logic.n; i++)
                    if (vi_syms[logic.s[i].sym].kind == VK_VAR) {
                        VSlot *sl = vi_slot(&regs, logic.s[i].sym);
                        sl->v = logic.s[i].v;
                        sl->mask = logic.s[i].mask;
                    }
                vi_env = &regs;
                vi_clocked = 1;
                vi_seq(cb2->child);
                vi_clocked = 0;
                continue;
            }
            vi_env = &logic;
            vi_seq(vi_node_copy(k, NULL));
        }
        vi_env = NULL;
        vi_bind_at = NULL;
        for (i = 0; i < logic.n; i++)
            for (j = 0; j < regs.n; j++)
                if (logic.s[i].sym == regs.s[j].sym && vi_syms[logic.s[i].sym].kind != VK_VAR)
                    vi_fail(at, "%s is given a value both on the clock edge and outside it; give it its values in one of the two",
                            vi_syms[logic.s[i].sym].wname);
        vi_emit_logic(at, &logic);
        if (rlist) {
            gram_node *rb = (gram_node *)calloc(1, sizeof *rb);
            rb->rule = VHDL_R_sbody;
            rb->child = rlist;
            rbody = rb;
        }
        vi_emit_regs(at->text, at, &regs, clk, neg, rst, rbody);
    } else {
        if (clocked) vi_zregs(cbody->child, at->text);
        if (clocked && rbody) vi_zregs(rbody->child, at->text);
        vi_clocked = clocked;
        vi_env = &env;
        vi_bind_at = at->text;
        vi_seq(clocked ? cbody->child : first);
        vi_bind_at = NULL;
        vi_env = NULL;
        vi_clocked = 0;
        if (!clocked) vi_emit_logic(at, &env);
        else vi_emit_regs(at->text, at, &env, clk, neg, rst, rbody);
    }
    vi_capturing = 0;
    vi_proc_out(st, at->text);
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
            int shape = vi_proc_shape(b, &c, &neg, &r, &rb, &cb);
            if (shape) {
                if (*n < 64) out[(*n)++] = c;
                if (!depth && shape == 1) vi_scan_regs(cb->child);
                if (!depth && shape == 2) {   /* each clock edge's registers */
                    gram_node *q;
                    for (q = b; q; q = q->next) if (q->rule == VHDL_R_ifst && vi_edge_if(q, &c, &neg, &r, &rb, &cb)) vi_scan_regs(cb->child);
                }
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

/* the writes in a clocked process that is nothing but ram writes: every
 * statement is arr(i) <= v, arr(i)(7 downto 0) <= v or arr(i)(3) <= v, inside
 * ifs without else (if we = '1' then if be(0) = '1' then ...). Those of the
 * array called name go in lanes (if lanes is not NULL); the others are
 * counted. 0 if the process is not of that shape. */
static int vi_same_id(const gram_node *a, const gram_node *b);

/* does n give name a value anywhere in it? */
static int vi_writes(const gram_node *n, const gram_node *name)
{
    const gram_node *k;
    if (n->rule == VHDL_R_sassign && vi_same_id(n->child->child, name)) return 1;
    for (k = n->child; k; k = k->next) if (vi_writes(k, name)) return 1;
    return 0;
}

/* the writes of array name in a clocked process: each must be name(addr) <= x
 * (or a bit or slice of it) under ifs without else, all at one address; the
 * rest of the process is read as usual */
static int vi_ram_walk(gram_node *first, const gram_node *name, gram_node **conds, int ncond,
                       VLane *lanes, int *nl, int *nother, gram_node **addr)
{
    gram_node *st;
    for (st = first; st; st = st->next) {
        if (st->rule == VHDL_R_ident || st->rule == VHDL_R_nullst || st->rule == VHDL_R_assert || st->rule == VHDL_R_reportst) continue;
        if (name && st->rule != VHDL_R_sassign && !vi_writes(st, name)) { (*nother)++; continue; }   /* not about the ram */
        if (st->rule == VHDL_R_ifst) {   /* if c then ... end if, nothing else */
            gram_node *c = st->child, *body;
            if (c->rule == VHDL_R_label) c = c->next;
            body = c->next;
            if (body->next && body->next->rule != VHDL_R_ident) return 0;
            if (ncond == VI_MAXCOND) return 0;
            conds[ncond] = c;
            if (!vi_ram_walk(body->child, name, conds, ncond + 1, lanes, nl, nother, addr)) return 0;
            continue;
        }
        if (st->rule == VHDL_R_sassign) {
            gram_node *tg = st->child, *id = tg->child, *v;
            if (name && !vi_same_id(id, name)) { (*nother)++; continue; }   /* another signal: read as usual */
            v = tg->next->rule == VHDL_R_wave ? tg->next->child : tg->next;
            if (v->next) return 0;   /* after ... */
            if (!id->next || id->next->rule == VHDL_R_drange) return 0;   /* not arr(i) */
            if (id->next->next && id->next->next->next) return 0;
            if (!name || !vi_same_id(id, name)) { (*nother)++; continue; }
            if (*addr && !vi_txteq(*addr, id->next)) return 0;   /* one address for all its writes */
            *addr = id->next;
            if (lanes) {
                VLane *l = &lanes[*nl];
                int i;
                if (*nl == 64) return 0;
                memset(l, 0, sizeof *l);
                l->din = v;
                l->ncond = ncond;
                for (i = 0; i < ncond; i++) l->cond[i] = conds[i];
                l->lo = -1;   /* whole word, filled in later */
                if (id->next->next) { l->hi = -2; l->cond[VI_MAXCOND - 1] = id->next->next; }   /* the bit or slice */
            }
            (*nl)++;
            continue;
        }
        return 0;
    }
    return 1;
}

/* the bits a lane gives: mem(i) all of them, mem(i)(k) one, mem(i)(a downto b) a slice */
static int vi_lane_bits(VLane *l, const VSym *e, const gram_node *name)
{
    int lo = vi_low(e), hi = vi_high(e);
    gram_node *sel;
    long a, b;
    if (l->hi != -2) { l->hi = e->w - 1; l->lo = 0; return 1; }
    sel = l->cond[VI_MAXCOND - 1];
    l->cond[VI_MAXCOND - 1] = NULL;
    if (!vi_vec(e->ty)) return 0;
    if (sel->rule == VHDL_R_drange) {
        int down = vi_is(sel->child->next, "downto");
        if (!vi_ceval(sel->child, &a) || !vi_ceval(sel->child->next->next, &b)) return 0;
        if (down != e->down || (down && a < b) || (!down && a > b)) return 0;
    } else {
        if (!vi_ceval(sel, &a)) return 0;
        b = a;
        l->bit = 1;
    }
    if (a < lo || a > hi || b < lo || b > hi) return 0;
    a = e->down ? a - lo : hi - a;
    b = e->down ? b - lo : hi - b;
    l->hi = (int)(a > b ? a : b);
    l->lo = (int)(a > b ? b : a);
    (void)name;
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

static int vi_lane_cmp(const void *a, const void *b) { return ((const VLane *)a)->lo - ((const VLane *)b)->lo; }

/* the array signal s (declared as id, with initial value init): is it a ram?
 * Written in one clocked process at one address, read only at that address.
 * Each write becomes a ram part of its own (byte lanes); together they give
 * every bit of a word, each bit once. */
static int vi_ram_find(VSym *s, gram_node *id, gram_node *init)
{
    VType *t = &vi_types[s->ut - 1];
    gram_node *k, *found = NULL, *first, *addr = NULL, *conds[VI_MAXCOND];
    VLane lanes[64];
    int nl = 0, i;
    long c;
    if (!vi_cur || t->alo != 0 || vi_suffix[0]) return 0;
    if (t->elem.ty != T_SLV && t->elem.ty != T_UNS && t->elem.ty != T_SGN) return 0;
    first = vi_cur->arch->child->next->next;
    for (k = first; k; k = k->next) {
        gram_node *decls, *body, *clk, *rst, *rbody, *cbody, *a = NULL;
        int neg, n = 0, other = 0;
        if (k->rule != VHDL_R_process) continue;
        body = vi_proc_body(k, &decls);
        if (vi_proc_shape(body, &clk, &neg, &rst, &rbody, &cbody) != 1) continue;
        if (rst && vi_writes(rbody, id)) continue;   /* a ram is not reset */
        if (!vi_ram_walk(cbody->child, id, conds, 0, NULL, &n, &other, &a) || !n) continue;
        if (found) return 0;   /* written by two processes */
        found = k;
        addr = NULL;
        vi_ram_walk(cbody->child, id, conds, 0, lanes, &nl, &other, &addr);
        s->ramproc = k; s->ramaddr = addr; s->ramclk = clk; s->ramneg = neg; s->ramrst = rst;
    }
    if (!found || vi_ceval(s->ramaddr, &c)) goto no;
    for (i = 0; i < nl; i++) {   /* no part of the ram's own write may read it */
        int j;
        if (!vi_lane_bits(&lanes[i], &t->elem, id)) goto no;
        if (!vi_ram_uses(lanes[i].din, NULL, id, NULL, NULL)) goto no;
        for (j = 0; j < lanes[i].ncond; j++) if (!vi_ram_uses(lanes[i].cond[j], NULL, id, NULL, NULL)) goto no;
    }
    qsort(lanes, (size_t)nl, sizeof *lanes, vi_lane_cmp);
    for (i = 0; i < nl; i++)   /* every bit once */
        if (lanes[i].lo != (i ? lanes[i - 1].hi + 1 : 0)) goto no;
    if (lanes[nl - 1].hi != t->elem.w - 1) goto no;
    for (k = first; k; k = k->next) {
        if (vi_is_decl(k->rule) || k->rule == VHDL_R_ident) continue;
        if (!vi_ram_uses(k, NULL, id, s->ramaddr, found)) goto no;
    }
    s->lanes = (VLane *)malloc((size_t)nl * sizeof *s->lanes);
    memcpy(s->lanes, lanes, (size_t)nl * sizeof *s->lanes);
    s->nlanes = nl;
    s->raminit = init;
    return 1;
no:
    s->ramproc = NULL;
    return 0;
}

/* a ram read: the word, its lanes side by side */
static char *vi_ram_word(const VSym *s)
{
    VBuf b = { 0, 0, 0 };
    int i;
    if (s->nlanes == 1) return s->lanes[0].wname;
    vb_add(&b, "{", 1);
    for (i = s->nlanes - 1; i >= 0; i--) vbf(&b, "%s%s", i == s->nlanes - 1 ? "" : ", ", s->lanes[i].wname);
    vb_add(&b, "}", 1);
    return b.s;
}

/* the ram parts, where their process was */
static void vi_ram_emit(VSym *s)
{
    VType *t = &vi_types[s->ut - 1];
    gram_node *at = vi_kid(s->ramproc, VHDL_R_PROCESS) ? vi_kid(s->ramproc, VHDL_R_PROCESS) : s->ramproc;
    VSym *cs = vi_find(s->ramclk);
    VX ix;
    const char *clk;
    int j, n, li;
    ix = vi_gen(s->ramaddr, -1);
    if (ix.ty != T_INTV && ix.ty != T_UNS)
        vi_fail(s->ramaddr, "an index is an integer: write %s(to_integer(...))", s->wname);
    if (ix.w > 16) vi_fail(s->ramaddr, "a ram has up to 16 address bits, and this index has %d", ix.w);
    n = 1 << ix.w;
    if (!cs || cs->w != 1 || cs->ty == T_ARR || cs->kind == VK_VAR)
        vi_fail(s->ramclk, "the clock must be a one-bit signal or port");
    clk = s->ramneg ? vi_fmt("~%s", cs->wname) : cs->wname;
    if (s->screenw && s->nlanes > 1)
        vi_fail(s->ramaddr, "a screen is written a whole pixel at a time: %s(to_integer(addr)) <= pixel", s->wname);
    for (li = 0; li < s->nlanes; li++) {
        VLane *l = &s->lanes[li];
        VTarget tt;
        VX d;
        VBuf data = { 0, 0, 0 }, we = { 0, 0, 0 };
        int any = 0, lw = l->hi - l->lo + 1;
        memset(&tt, 0, sizeof tt);
        tt.s = s->wname;
        tt.ty = l->bit ? T_SL : t->elem.ty;
        tt.w = lw;
        d = vi_gen(l->din, tt.w);
        vi_assignable(&tt, &d, l->din);
        for (j = 0; j < l->ncond; j++) {
            VX c = vi_cond(l->cond[j]);
            vbf(&we, "%s%s", j ? " & " : "", vi_wrap(&c, 1));
        }
        if (s->ramrst) {   /* written on the clock edge, which the reset comes before */
            VX r = vi_cond(s->ramrst), nr = vi_not(r);
            vbf(&we, "%s%s", l->ncond ? " & " : "", vi_wrap(&nr, 1));
        } else if (!l->ncond) vb_add(&we, "1", 1);
        if (s->raminit) {   /* the words it starts with */
            for (j = 0; j < n && j <= t->ahi; j++) {
                VX x = vi_array_elem(t, s->raminit, j, s->raminit);
                unsigned long v;
                if (!x.cst) vi_fail(s->raminit, "a starting value must be a constant");
                v = (x.v >> l->lo) & vi_mask(lw);
                if (v) any = 1;
                vbf(&data, "%s%lX", j ? " " : "", v);
            }
        }
        if (s->screenw)   /* attribute wire_screen of it: drawn as a screen */
            vi_emit(at, "%s = screen(%s, %s, %s, %s, width = %d%s%s%s%s)", l->wname, ix.s, d.s, we.s, clk, s->screenw,
                    s->screentext ? ", text = 1" : "", any ? ", data = \"" : "", any ? data.s : "", any ? "\"" : "");
        else
            vi_emit(at, "%s = ram(%s, %s, %s, %s%s%s%s)", l->wname, ix.s, d.s, we.s, clk,
                    any ? ", data = \"" : "", any ? data.s : "", any ? "\"" : "");
    }
}


/* attribute wire_cpu of rtl : architecture is "isa=rv32im.isa; text=instr; data=ram; data_base=0x1000; pc=pc; halt=halted; regs=regs_%d";
 * becomes Wire's cpu statement (see wire.gram). text, pc and halt are
 * signals or ports; data is the ram array (or several, most significant byte
 * first), which becomes its byte lanes; isa and regs are text; the bases are
 * numbers. */
static VSym *vi_find_str(const char *s, int n)
{
    gram_node tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.text = s; tmp.len = (size_t)n;
    return vi_find(&tmp);
}

static void vi_emit_cpu(const VEnt *e)
{
    static const char *keys[] = { "isa", "text", "data", "text_base", "data_base", "pc", "halt", "regs" };
    char *val[8] = { 0 }, *p = vi_cpu_spec, *q;
    VBuf out = { 0, 0, 0 };
    int k, first = 1;
    if (!vi_cpu_spec) return;
    if (!e->top) vi_fail(vi_cpu_at, "wire_cpu goes in the architecture of the top entity, the one nobody uses");
    while (*p) {
        char *end = strchr(p, ';'), *eq;
        int len = end ? (int)(end - p) : (int)strlen(p), kl;
        while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        eq = len ? (char *)memchr(p, '=', (size_t)len) : NULL;
        if (len && !eq) vi_fail(vi_cpu_at, "wire_cpu: write each setting as name=value, separated by ;");
        if (len) {
            char *v;
            int vl = len - (int)(eq - p) - 1;
            kl = (int)(eq - p);
            while (kl && (p[kl - 1] == ' ' || p[kl - 1] == '\t')) kl--;
            for (k = 0; k < 8; k++) if ((int)strlen(keys[k]) == kl && !memcmp(keys[k], p, (size_t)kl)) break;
            if (k == 8) vi_fail(vi_cpu_at, "wire_cpu has no setting called %.*s (it has isa, text, data, text_base, data_base, pc, halt, regs)", kl, p);
            v = eq + 1; while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
            while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) vl--;
            val[k] = vi_fmt("%.*s", vl, v);
        }
        if (!end) break;
        p = end + 1;
    }
    if (!val[0]) vi_fail(vi_cpu_at, "wire_cpu needs isa=file.isa, the file that describes the instructions");
    if (!val[1]) vi_fail(vi_cpu_at, "wire_cpu needs text=, the signal the program memory (a rom) drives");
    vbf(&out, "cpu %s (isa = \"%s\"", e->wname, val[0]);
    for (k = 1; k < 8; k++) {
        if (!val[k]) continue;
        if (k == 7) { vbf(&out, ", regs = \"%s\"", val[k]); continue; }
        if (k == 3 || k == 4) { vbf(&out, ", %s = %s", keys[k], val[k]); continue; }
        if (k == 2) {   /* ram arrays, most significant first: their lanes, most significant first */
            VBuf lanes = { 0, 0, 0 };
            int n = 0;
            for (q = val[k]; *q;) {
                char *c = strchr(q, ',');
                int l = c ? (int)(c - q) : (int)strlen(q);
                VSym *s;
                while (l && *q == ' ') { q++; l--; }
                while (l && q[l - 1] == ' ') l--;
                s = vi_find_str(q, l);
                if (!s) vi_fail(vi_cpu_at, "wire_cpu: %.*s is not declared", l, q);
                if (!s->ramproc) vi_fail(vi_cpu_at, "wire_cpu: %.*s is not a ram (an array written by one clocked process)", l, q);
                for (first = s->nlanes - 1; first >= 0; first--) vbf(&lanes, "%s%s", n++ ? ", " : "", s->lanes[first].wname);
                if (!c) break;
                q = c + 1;
            }
            vbf(&out, ", data = {%s}", lanes.s ? lanes.s : "");
            continue;
        }
        {   /* text, pc, halt: a signal or port, by its Wire name */
            VSym *s = vi_find_str(val[k], (int)strlen(val[k]));
            if (!s) vi_fail(vi_cpu_at, "wire_cpu: %s is not declared", val[k]);
            vbf(&out, ", %s = %s", keys[k], s->wname);
        }
    }
    vb_add(&out, ")", 1);
    vi_emit(vi_cpu_at, "%s", out.s);
}

/* is there more in a ram's process than its ram writes (under ifs)? */
static int vi_only_rams(gram_node *first, gram_node *proc)
{
    gram_node *st;
    for (st = first; st; st = st->next) {
        if (st->rule == VHDL_R_ident || st->rule == VHDL_R_nullst || st->rule == VHDL_R_assert || st->rule == VHDL_R_reportst) continue;
        if (st->rule == VHDL_R_ifst) {
            gram_node *c = st->child;
            if (c->rule == VHDL_R_label) c = c->next;
            if (c->next->next && c->next->next->rule != VHDL_R_ident) return 0;
            if (!vi_only_rams(c->next->child, proc)) return 0;
            continue;
        }
        if (st->rule == VHDL_R_sassign) {
            VSym *a = vi_find(st->child->child);
            if (a && a->ty == T_ARR && a->ramproc == proc) continue;
        }
        return 0;
    }
    return 1;
}

static int vi_ram_rest(gram_node *st)
{
    gram_node *decls, *body = vi_proc_body(st, &decls), *clk, *rst, *rbody, *cbody;
    int neg;
    vi_proc_shape(body, &clk, &neg, &rst, &rbody, &cbody);
    return rst || !vi_only_rams(cbody->child, st);
}

static void vi_statement(gram_node *st)
{
    vi_stmt_at = st->text;
    if (st->rule == VHDL_R_process) {   /* the writes of rams: their ram parts */
        int i, nram = 0, lanes = 0;
        for (i = 0; i < vi_nsyms; i++)
            if (vi_syms[i].ramproc == st) { vi_ram_emit(&vi_syms[i]); nram++; lanes += vi_syms[i].nlanes; }
        (void)lanes;
        if (nram && !vi_ram_rest(st)) return;   /* only ram writes: nothing else to read */
    }
    switch (st->rule) {
    case VHDL_R_condassign: vi_condassign(st); break;
    case VHDL_R_selassign: vi_selassign(st); break;
    case VHDL_R_assert: break;   /* only for simulation */
    case VHDL_R_process: case VHDL_R_cpcall: vi_process(st); break;
    case VHDL_R_generate: vi_generate(st); break;
    case VHDL_R_inst: vi_inst(st); break;
    default:
        vi_fail(st, "this kind of statement is not supported yet");
    }
}

/* ---- drivers: a signal given values by several statements is a bus ----------------
 * In VHDL several statements may drive one std_logic signal (each with 'Z' when
 * it is not talking). Before an architecture is read, the statements that give
 * each name a whole value are counted: a process counts once, a for generate as
 * many (2 is enough to tell), a port map once per output or inout it gives the
 * name to. A signal with two or more becomes a Wire bus; Wire then checks that
 * every one of them is a tri(). A bus with one driver is a plain wire, so
 * counting one too many does no harm. */

typedef struct { const char *t; int n, whole; } VDrv;
static VDrv *vi_drv;

static void vi_drv_addt(const char *t, int n, int mult)
{
    int i;
    for (i = 0; i < vi_ndrv; i++) {
        int j;
        if (vi_drv[i].n != n) continue;
        for (j = 0; j < n && vi_lower(vi_drv[i].t[j]) == vi_lower(t[j]); j++) {}
        if (j == n) { vi_drv[i].whole += mult; return; }
    }
    vi_drv = (VDrv *)realloc(vi_drv, (size_t)(vi_ndrv + 1) * sizeof *vi_drv);
    vi_drv[vi_ndrv].t = t; vi_drv[vi_ndrv].n = n; vi_drv[vi_ndrv++].whole = mult;
}

static void vi_drv_add(const gram_node *id, int mult) { vi_drv_addt(id->text, (int)id->len, mult); }

/* an if ... generate's condition, if the generics already tell it: 1 or 0 (-1: not known yet) */
static int vi_drv_cond(const gram_node *c)
{
    long a, b;
    if (c->rule == VHDL_R_relation && vi_ceval(c->child, &a) && vi_ceval(c->child->next->next, &b)) {
        const gram_node *op = c->child->next;
        if (vi_is(op, "=")) return a == b;
        if (vi_is(op, "/=")) return a != b;
        if (vi_is(op, "<")) return a < b;
        if (vi_is(op, "<=")) return a <= b;
        if (vi_is(op, ">")) return a > b;
        if (vi_is(op, ">=")) return a >= b;
    }
    if (c->rule == VHDL_R_ident && vi_find(c) && vi_find(c)->kind == VK_GEN && vi_find(c)->ty == T_BOOL) return (int)(vi_find(c)->ival & 1);
    return -1;
}

static int vi_drivers(const char *name, int n)
{
    int i;
    for (i = 0; i < vi_ndrv; i++) {
        int j;
        if (vi_drv[i].n != n) continue;
        for (j = 0; j < n && vi_lower(vi_drv[i].t[j]) == vi_lower(name[j]); j++) {}
        if (j == n) return vi_drv[i].whole;
    }
    return 0;
}

/* the names a process gives values to, each once */
static void vi_drv_seq(gram_node *n, const gram_node **seen, int *ns)
{
    gram_node *k;
    for (k = n; k; k = k->next) {
        if (k->rule == VHDL_R_sassign) {
            const gram_node *id = k->child->child;
            int i;
            for (i = 0; i < *ns; i++) if (seen[i]->len == id->len && vi_is(seen[i], vi_fmt("%.*s", VT(id)))) break;
            if (i == *ns && *ns < 256) seen[(*ns)++] = id;
        } else if (k->rule != VHDL_R_vassign && k->child) vi_drv_seq(k->child, seen, ns);
    }
}

static void vi_drv_scan(gram_node *first, int mult)
{
    gram_node *k;
    for (k = first; k; k = k->next) {
        gram_node *c = k->child;
        if (k->rule == VHDL_R_condassign || k->rule == VHDL_R_selassign) {
            if (c->rule == VHDL_R_label) c = c->next;
            if (k->rule == VHDL_R_selassign) c = c->next;
            if (!c->child->next) vi_drv_add(c->child, mult);   /* a slice of it is a part, not a driver of all of it */
        } else if (k->rule == VHDL_R_process) {
            const gram_node *seen[256];
            int ns = 0, i;
            gram_node *decls, *body = vi_proc_body(k, &decls);
            vi_drv_seq(body, seen, &ns);
            for (i = 0; i < ns; i++) vi_drv_add(seen[i], mult);
        } else if (k->rule == VHDL_R_cpcall) {   /* a procedure called on its own: the names it is given may be its outputs */
            gram_node *nm = c->rule == VHDL_R_label ? c->next : c, *as;
            if (nm->rule == VHDL_R_name && nm->child->next && nm->child->next->rule == VHDL_R_args)
                for (as = nm->child->next->child; as; as = as->next) {
                    gram_node *v = as->rule == VHDL_R_named ? as->child->next : as;
                    if (v->rule == VHDL_R_ident) vi_drv_add(v, mult);
                }
        } else if (k->rule == VHDL_R_generate) {
            gram_node *g = c->next;
            gram_node *b = g->child, *gd = NULL;
            for (; b; b = b->next) {
                if (b->rule == VHDL_R_gdecls) gd = b;
                if (b->rule == VHDL_R_cstmt || b->rule == VHDL_R_process || b->rule == VHDL_R_condassign || b->rule == VHDL_R_cpcall
                    || b->rule == VHDL_R_selassign || b->rule == VHDL_R_inst || b->rule == VHDL_R_generate) break;
            }
            if (g->rule == VHDL_R_ifgen) {   /* only when it is there */
                if (vi_drv_cond(g->child) != 0) vi_drv_scan(b, mult);
            } else {   /* once per copy: a signal declared inside has a copy of its own */
                VDrv *save = vi_drv, *loc;
                int ns = vi_ndrv, nl, i, copies = 2;
                long from, to;
                gram_node *r = g->child->next;
                if (r->rule == VHDL_R_drange && vi_ceval(r->child, &from) && vi_ceval(r->child->next->next, &to))
                    copies = (int)(from > to ? from - to + 1 : to - from + 1);
                vi_drv = NULL; vi_ndrv = 0;
                vi_drv_scan(b, 1);
                loc = vi_drv; nl = vi_ndrv;
                vi_drv = save; vi_ndrv = ns;
                for (i = 0; i < nl; i++) {
                    int local = 0;
                    gram_node *d, *id;
                    for (d = gd ? gd->child : NULL; d && !local; d = d->next)
                        if (d->rule == VHDL_R_signal)
                            for (id = d->child->child; id; id = id->next)
                                if ((int)id->len == loc[i].n && vi_is(id, vi_fmt("%.*s", loc[i].n, loc[i].t))) local = 1;
                    vi_drv_addt(loc[i].t, loc[i].n, local ? loc[i].whole * mult : loc[i].whole * copies * mult);
                }
                free(loc);
            }
        } else if (k->rule == VHDL_R_inst) {
            VEnt *e = vi_ent_named(vi_inst_name(k));
            gram_node *pm = vi_kid(k, VHDL_R_pmap), *as;
            int pos = 0, i;
            if (!e || !pm) continue;
            for (as = pm->child; as; as = as->next) {
                gram_node *f = as->child->next ? as->child : NULL, *v = f ? f->next : as->child;
                int q = 0;
                for (i = 0; i < e->nports; i++) {   /* positional: the record fields are not ports of their own */
                    if (e->ports[i].nlen < 0 || memchr(e->ports[i].name, '.', (size_t)e->ports[i].nlen)) continue;
                    if (q++ == pos) break;
                }
                pos++;
                if (f) for (i = 0; i < e->nports; i++)
                    if (e->ports[i].nlen == (int)f->child->len && vi_is(f->child, vi_fmt("%.*s", e->ports[i].nlen, e->ports[i].name))) break;
                if (i >= e->nports || e->ports[i].kind == VK_IN || e->ports[i].ty == T_REC || (f && f->child->next)) continue;
                if (v->rule == VHDL_R_ident) vi_drv_add(v, mult);
            }
        }
    }
}

/* ---- the design ---------------------------------------------------------------- */

/* ---- 7-segment displays ---------------------------------------------------
 * An output of the top entity that is a 7-segment pattern (7 bits, or 8 with
 * the dot, and called seg, sseg, segments, disp, hex0 ...) is shown as a
 * 7-segment display (seg7), not as a row of bits. How the board is wired is
 * read from the patterns the VHDL writes: every "0110000"-like literal of 7
 * or 8 bits is tried as lit-by-1 and lit-by-0, segment a on bit 0 and on bit
 * 6, and the wiring that makes the most of them digits (0-9, A-F) wins. */
static int vi_is_prefix(const gram_node *n, const char *s, size_t len)   /* starts with s, ignoring case */
{
    size_t i;
    if (n->len < len) return 0;
    for (i = 0; i < len; i++) if (vi_lower(n->text[i]) != vi_lower(s[i])) return 0;
    return 1;
}

static const unsigned char vi_digits[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x7C, 0x07, 0x27, 0x6F, 0x67,
                                            0x77, 0x39, 0x5E, 0x79, 0x71, 0x58, 0x40 };

static int vi_seg_name(const gram_node *id)
{
    static const char *pre[] = { "seg", "sseg", "seven", "7seg", "disp", "hex" };
    size_t i, n;
    for (i = 0; i < sizeof pre / sizeof pre[0]; i++) {
        n = strlen(pre[i]);
        if (id->len < n) continue;
        if (!vi_is_prefix(id, pre[i], n)) continue;
        if (!strcmp(pre[i], "hex")) {   /* hex0, HEX1 (DE boards), not hex itself (a value) */
            return id->len == n + 1 && id->text[n] >= '0' && id->text[n] <= '9';
        }
        if (!strcmp(pre[i], "seg") && id->len > n && !(id->text[n] == '_' || id->text[n] == 's' || id->text[n] == 'S'
            || (id->text[n] >= '0' && id->text[n] <= '9') || vi_is_prefix(id, "segment", 7))) return 0;
        return 1;
    }
    return 0;
}

static void vi_seg_style(int *low, int *rev)
{
    static int done, slow, srev;
    int score[4] = { 0, 0, 0, 0 }, k, best = 0;
    const char *p;
    if (!done) {
        done = 1;
        const char *end = vi_src + vi_len;
        for (p = vi_src; p < end; p++) {
            const char *q = p + 1;
            unsigned v = 0;
            int nb = 0, j, c;
            if (*p == '-' && p + 1 < end && p[1] == '-') { while (p < end && *p != '\n') p++; continue; }   /* a comment */
            if (*p != '"') continue;
            while (q < end && (*q == '0' || *q == '1' || *q == '_')) { if (*q != '_') { v = v << 1 | (unsigned)(*q - '0'); nb++; } q++; }
            if (q >= end) break;
            if (*q != '"' || (nb != 7 && nb != 8)) { p = *q == '"' ? q : p; continue; }
            p = q;
            v &= 0x7F;
            if (v == 0 || v == 0x7F) continue;   /* blank and 8 look the same either way */
            for (c = 0; c < 4; c++) {
                unsigned x = c & 1 ? v ^ 0x7F : v, r = 0;
                if (c & 2) { for (j = 0; j < 7; j++) if (x >> j & 1) r |= 1u << (6 - j); x = r; }
                for (j = 0; j < (int)sizeof vi_digits; j++) if (vi_digits[j] == x) { score[c]++; break; }
            }
        }
        for (k = 1; k < 4; k++) if (score[k] > score[best]) best = k;
        if (score[best] >= 3) { slow = best & 1; srev = best >> 1 & 1; }
    }
    *low = slow; *rev = srev;
}

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
            if (proto.ty != T_INT && proto.ty != T_BOOL && proto.ty != T_SL && !vi_vec(proto.ty))
                vi_fail(ty, "Wire reads generics that are integers, booleans, bits or vectors of up to 32 bits");
            if (proto.w > MAXW) vi_fail(ty, "a vector generic has up to %d bits", MAXW);
            if (!def) vi_fail(ids, "give %.*s a default value (:= 4); Wire builds the entity with its defaults",
                              VT(ids->child));
            for (id = ids->child; id; id = id->next) {
                VSym *s = vi_declare(id, VK_GEN);
                int k = 0, j;
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                if (proto.ty == T_INT) s->ival = vi_const_int(def, "a generic's default");
                else {   /* a boolean, bit or vector: its bits */
                    VX x = vi_gen(def, proto.w);
                    VTarget tt;
                    memset(&tt, 0, sizeof tt);
                    tt.s = vi_fmt("%.*s", VT(id)); tt.ty = proto.ty; tt.w = proto.w;
                    if (!x.cst) vi_fail(def, "a generic's default must be a constant");
                    vi_assignable(&tt, &x, def);
                    s->ival = (long)x.v;
                }
                for (j = 0; j < vi_nsyms - 1; j++) if (vi_syms[j].kind == VK_GEN) k++;
                if (vi_over) s->ival = vi_over[k];   /* a copy for a generic map */
                s->bits = (unsigned long)s->ival & vi_mask(s->w ? s->w : 32);
            }
            continue;
        }
        out = mode && (vi_is(mode, "out") || vi_is(mode, "buffer") || vi_is(mode, "inout") || vi_is(mode, "linkage"));
        vi_wide_ok = 1;
        vi_type(ty, &proto, 0);
        vi_wide_ok = 0;
        {
            VBuf c = { 0, 0, 0 }, g = { 0, 0, 0 };
            for (id = ids->child; id; id = id->next) {
                VSym *s = vi_declare(id, out ? VK_OUT : VK_IN);
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                s->intneg = proto.intneg;
                s->ut = proto.ut;
                s->def = def;
                if (proto.w > MAXW) {   /* wider than 32 bits: a pin per 32 bits (p_31_0, p_63_32), the port itself none */
                    int si = (int)(s - vi_syms), c, nch = (proto.w + 31) / 32;
                    if (mode && (vi_is(mode, "inout") || vi_is(mode, "linkage")))
                        vi_fail(ty, "an inout port has up to %d bits", MAXW);
                    if (def) vi_fail(def, "a port wider than %d bits has no default value", MAXW);
                    vi_syms[si].wch0 = vi_nsyms;
                    vi_syms[si].wnch = nch;
                    for (c = 0; c < nch; c++) {
                        VSym *e;
                        int lo = 32 * c, cw = proto.w - lo < 32 ? proto.w - lo : 32;
                        char *w = vi_fmt("%s_%d_%d", vi_syms[si].wname, lo + cw - 1, lo);
                        while (vi_wname_used(w)) w = vi_fmt("%s_", w);
                        if (vi_nsyms == vi_cap) { vi_cap = vi_cap ? vi_cap * 2 : 64; vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms); }
                        e = &vi_syms[vi_nsyms++];
                        memset(e, 0, sizeof *e);
                        e->kind = out ? VK_OUT : VK_IN;
                        e->ty = proto.ty; e->w = cw; e->left = cw - 1; e->right = 0; e->down = 1;
                        e->name = vi_syms[si].name; e->nlen = -1; e->wname = w; e->at = id;
                        vbf(&b, "%s%s[%d]", b.n ? ", " : "", w, cw);
                    }
                    continue;
                }
                if (s->ty == T_REC) {   /* a record: a pin per field (p_a, p_b), the record itself none */
                    int si = (int)(s - vi_syms), f;
                    VType *rt = &vi_types[s->ut - 1];
                    for (f = 0; f < rt->nf; f++)
                        if (rt->fty[f].ty == T_ARR) vi_fail(ty, "a record port has fields of bits, vectors and records (not arrays)");
                    vi_rec_fields(si, out ? VK_OUT : VK_IN, NULL, 0);
                    for (f = si + 1; f < vi_nsyms; f++) {   /* its fields, and the fields of records in it */
                        VSym *e = &vi_syms[f];
                        if (e->ty == T_REC) continue;
                        if (e->w > 1 || vi_vec(e->ty)) vbf(&b, "%s%s[%d]", b.n ? ", " : "", e->wname, e->w);
                        else vbf(&b, "%s%s", b.n ? ", " : "", e->wname);
                    }
                    continue;
                }
                if (mode && (vi_is(mode, "inout") || vi_is(mode, "linkage"))) {   /* a pin both ways */
                    if (s->ty != T_SL && s->ty != T_SLV)
                        vi_fail(ty, "an inout port is std_logic or std_logic_vector (its drivers give 'Z' when they are not talking)");
                    s->inout = 1;
                }
                if (!out && s->w == 1 && !vi_vec(s->ty) && vi_is_clock(id)) { vbf(&c, "%s%s", c.n ? ", " : "", s->wname); continue; }
                if (out && emit && (s->w == 7 || s->w == 8) && vi_vec(s->ty) && vi_seg_name(id)) {   /* a 7-segment display */
                    int low, rev;
                    vi_seg_style(&low, &rev);
                    vbf(&g, "%s%s[%d]%s%s%s%s", g.n ? ", " : "", s->wname, s->w, low || rev ? " (" : "", low ? "on = 0" : "",
                        low && rev ? ", " : "", rev ? "a = 6)" : low ? ")" : "");
                    continue;
                }
                if (s->w > 1 || vi_vec(s->ty) || (out && vi_is_reg(id))) vbf(&b, "%s%s[%d]", b.n ? ", " : "", s->wname, s->w);
                else vbf(&b, "%s%s", b.n ? ", " : "", s->wname);
            }
            if (emit && c.n) vi_emit(ids, "clock %s", c.s);   /* a clock of a process: Run and Step on the page */
            if (emit && g.n) vi_emit(ids, "seg7 %s", g.s);
            if (emit && b.n) vi_emit(ids, "%s %s", out ? "output" : "input", b.s);
        }
    }
    if (!is_generic) {   /* inout ports: p_in and p_oe, named once every port has its name */
        int i;
        for (i = 0; i < vi_nsyms; i++) {
            VSym *s = &vi_syms[i];
            char *r, *o;
            if (!s->inout || s->rname) continue;
            r = vi_fmt("%s_in", s->wname);
            while (vi_wname_used(r)) r = vi_fmt("%s_", r);
            s->rname = r;
            o = vi_fmt("%s_oe", s->wname);
            while (vi_wname_used(o)) o = vi_fmt("%s_", o);
            s->roe = o;
            if (emit) {   /* the top: the page drives it through p_in (while p_oe is 1) and shows the bus */
                if (vi_vec(s->ty)) vi_emit(s->at, "input %s[%d], %s", r, s->w, o);
                else vi_emit(s->at, "input %s, %s", r, o);
            }
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
            VBuf b = { 0, 0, 0 }, bus = { 0, 0, 0 };
            memset(&proto, 0, sizeof proto);
            vi_wide_ok = 1;
            vi_type(ty, &proto, 2);
            vi_wide_ok = 0;
            for (id = d->child->child; id; id = id->next) {
                VSym *s = vi_declare(id, var ? VK_VAR : VK_SIG);
                int si = (int)(s - vi_syms);
                VBuf *wb = !var && vi_drivers(id->text, (int)id->len) > 1 ? &bus : &b;   /* several drivers: a bus */
                if (proto.ty == T_REC) {   /* a record: a signal (or variable) per field */
                    s->ty = T_REC; s->ut = proto.ut; s->w = proto.w;
                    vi_rec_fields(si, var ? VK_VAR : VK_SIG, wb, !var);
                    if (init) {   /* each field starts with its part */
                        VType *rt = &vi_types[proto.ut - 1];
                        int f;
                        for (f = 0; f < rt->nf; f++) {
                            VSym *e = &vi_syms[vi_syms[si].fld0 + f];
                            gram_node *pv = vi_rec_part(init, rt, f);
                            VX x;
                            if (e->ty == T_REC || e->ty == T_ARR) continue;
                            x = vi_gen(pv, e->w);
                            if (!x.cst) vi_fail(pv, "a starting value must be a constant");
                            e->bits = x.v;
                            e->hasinit = 1;
                        }
                    }
                    continue;
                }
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                s->intneg = proto.intneg;
                s->ut = proto.ut;
                if (proto.w > MAXW) {   /* a Wire wire (or variable) per 32 bits: prod_31_0, prod_63_32 */
                    int c, nch = (proto.w + 31) / 32;
                    vi_syms[si].wch0 = vi_nsyms;
                    vi_syms[si].wnch = nch;
                    for (c = 0; c < nch; c++) {
                        VSym *e;
                        int lo = 32 * c, cw = proto.w - lo < 32 ? proto.w - lo : 32;
                        char *w = vi_fmt("%s_%d_%d", vi_syms[si].wname, lo + cw - 1, lo);
                        while (vi_wname_used(w)) w = vi_fmt("%s_", w);
                        if (vi_nsyms == vi_cap) {
                            vi_cap = vi_cap ? vi_cap * 2 : 64;
                            vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms);
                        }
                        e = &vi_syms[vi_nsyms++];
                        memset(e, 0, sizeof *e);
                        e->kind = var ? VK_VAR : VK_SIG;
                        e->ty = proto.ty; e->w = cw; e->left = cw - 1; e->right = 0; e->down = 1;
                        e->name = vi_syms[si].name; e->nlen = -1; e->wname = w; e->at = id;
                        if (!var) vbf(wb, "%s%s[%d]", wb->n ? ", " : "", w, cw);
                    }
                    if (init) {
                        VTarget t;
                        VX x;
                        memset(&t, 0, sizeof t);
                        t.s = vi_syms[si].wname; t.ty = proto.ty; t.w = proto.w;
                        x = vi_gen_any(init, proto.w);
                        vi_assignable(&t, &x, init);
                        if (!x.chv) vi_fail(init, "a starting value must be a constant");
                        for (c = 0; c < nch; c++) {
                            vi_syms[vi_syms[si].wch0 + c].bits = x.chv[c];
                            vi_syms[vi_syms[si].wch0 + c].hasinit = 1;
                        }
                    }
                    continue;
                }
                if (s->ty == T_ARR && !var && vi_ram_find(s, id, init)) {   /* one ram part */
                    VType *t = &vi_types[proto.ut - 1];
                    size_t L = strlen(vi_syms[si].wname);
                    /* Wire writes its ram called mem as mem_words: it is mem again */
                    if (L > 6 && !strcmp(vi_syms[si].wname + L - 6, "_words")) {
                        char *w = vi_fmt("%.*s", (int)(L - 6), vi_syms[si].wname);
                        if (!vi_wire_word(w, (int)strlen(w)) && !vi_wname_used(w)) vi_syms[si].wname = w;
                    }
                    if (vi_syms[si].nlanes == 1) {
                        vi_syms[si].lanes[0].wname = vi_syms[si].wname;
                        vbf(&b, "%s%s[%d]", b.n ? ", " : "", vi_syms[si].wname, t->elem.w);
                    } else {   /* a ram part per lane: mem_b0, mem_b8 ... (named for its lowest bit) */
                        int li;
                        for (li = 0; li < vi_syms[si].nlanes; li++) {
                            VLane *l = &vi_syms[si].lanes[li];
                            char *w = vi_fmt("%s_b%d", vi_syms[si].wname, l->lo);
                            VSym *e;
                            while (vi_wname_used(w)) w = vi_fmt("%s_", w);
                            if (vi_nsyms == vi_cap) {
                                vi_cap = vi_cap ? vi_cap * 2 : 64;
                                vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms);
                            }
                            e = &vi_syms[vi_nsyms++];   /* holds the name, so nothing else takes it */
                            memset(e, 0, sizeof *e);
                            e->kind = VK_SIG; e->ty = T_SLV; e->w = l->hi - l->lo + 1;
                            e->name = vi_syms[si].name; e->nlen = -1; e->wname = w; e->at = id;
                            vi_syms[si].lanes[li].wname = w;
                            vbf(&b, "%s%s[%d]", b.n ? ", " : "", w, l->hi - l->lo + 1);
                        }
                    }
                    continue;
                }
                if (s->ty == T_ARR) {   /* a signal (or variable) per element: mem_0, mem_1 ... (mem_0_3 for two indexes) */
                    VType *t = &vi_types[proto.ut - 1];
                    int k, ne = vi_nelems(t);
                    vi_syms[si].elem0 = vi_nsyms;
                    for (k = 0; k < ne; k++) {
                        VSym *e;
                        char *w = t->two ? vi_fmt("%s_%d_%d", vi_syms[si].wname, t->alo + k / (t->bhi - t->blo + 1),
                                                  t->blo + k % (t->bhi - t->blo + 1))
                                         : vi_fmt("%s_%d", vi_syms[si].wname, t->alo + k);
                        while (vi_wname_used(w)) w = vi_fmt("%s_", w);
                        if (vi_nsyms == vi_cap) {
                            vi_cap = vi_cap ? vi_cap * 2 : 64;
                            vi_syms = (VSym *)realloc(vi_syms, (size_t)vi_cap * sizeof *vi_syms);
                        }
                        e = &vi_syms[vi_nsyms++];
                        *e = t->elem;
                        e->kind = var ? VK_VAR : VK_SIG;
                        e->name = vi_syms[si].name;
                        e->nlen = -1;   /* found through the array, not by name */
                        e->wname = w;
                        e->at = id;
                        if (!var) vbf(wb, "%s%s[%d]", wb->n ? ", " : "", w, e->w);
                    }
                    if (init) {   /* the values its elements start with */
                        for (k = 0; k < ne; k++) {
                            VX x = vi_array_elem(t, init, k, init);
                            VSym *e = &vi_syms[vi_syms[si].elem0 + k];
                            if (!x.cst) vi_fail(init, "a starting value must be a constant");
                            e->bits = x.v;
                            e->hasinit = 1;
                        }
                    }
                    continue;
                }
                if (init) {   /* the value it starts with: a register without a reset starts there */
                    VTarget t;
                    VX x;
                    memset(&t, 0, sizeof t);
                    t.s = vi_syms[si].wname; t.ty = proto.ty; t.w = proto.w; t.ut = proto.ut;
                    vi_zok = !var;
                    x = vi_gen(init, proto.w);
                    vi_zok = 0;
                    if (!x.z) {   /* := (others => 'Z'): it starts not driven, which is how every bus starts */
                        if (!x.cst) vi_fail(init, "a starting value must be a constant");
                        vi_assignable(&t, &x, init);
                        vi_syms[si].bits = x.v;
                        vi_syms[si].hasinit = 1;
                    }
                }
                if (var) continue;   /* a variable is only a value inside its process */
                /* always with its width: a register's width cannot be inferred from itself */
                vbf(wb, "%s%s[%d]", wb->n ? ", " : "", vi_syms[si].wname, proto.w);
            }
            if (b.n) vi_emit(d->child, "wire %s", b.s);
            if (bus.n) vi_emit(d->child, "bus %s", bus.s);
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
            } else if (def->rule == VHDL_R_rangedef) {   /* type small is range 0 to 15 */
                int down;
                t.isint = 1;
                vi_range(def->child, &t.ilo, &t.ihi, &down, "an integer type's range");
                if (down) { long q = t.ilo; t.ilo = t.ihi; t.ihi = q; }
            } else if (def->rule == VHDL_R_recdef) {   /* record a : t; b : t; end record */
                gram_node *el, *fid;
                for (el = def->child; el && el->rule == VHDL_R_recel; el = el->next) {
                    VSym proto;
                    memset(&proto, 0, sizeof proto);
                    vi_type(vi_kid(el, VHDL_R_type), &proto, 2);
                    if (proto.ty == T_ARR) vi_fail(el, "an array in a record is not supported yet; give the record a vector instead");
                    for (fid = vi_kid(el, VHDL_R_idlist)->child; fid; fid = fid->next) {
                        int k;
                        for (k = 0; k < t.nf; k++) if (vi_is(fid, vi_fmt("%.*s", VT(t.fid[k])))) vi_fail(fid, "%.*s is a field twice", VT(fid));
                        t.fid = (gram_node **)realloc(t.fid, (size_t)(t.nf + 1) * sizeof *t.fid);
                        t.fty = (VSym *)realloc(t.fty, (size_t)(t.nf + 1) * sizeof *t.fty);
                        t.fid[t.nf] = fid;
                        t.fty[t.nf++] = proto;
                    }
                }
                t.isrec = 1;
            } else {   /* array (0 to 15) of t, array (0 to 3, 0 to 7) of t, array (natural range <>) of t */
                gram_node *r = def->child, *ety;
                for (ety = r; ety->next; ety = ety->next) {}
                t.isarr = 1;
                if (r->rule == VHDL_R_unbound) {
                    if (r->next != ety) vi_fail(r, "an array without a range has one index");
                    t.unbound = 1;
                } else {
                    long a, z;
                    int down;
                    vi_range(r, &a, &z, &down, "an array's range");
                    t.alo = (int)(a < z ? a : z);
                    t.ahi = (int)(a < z ? z : a);
                    t.adown = down;
                    if (r->next != ety) {   /* two indexes: element (i, j) is i * columns + j, row by row */
                        gram_node *r2 = r->next;
                        if (r2->next != ety) vi_fail(r2->next, "Wire handles arrays with one or two indexes");
                        if (r2->rule == VHDL_R_unbound) vi_fail(r2, "give this index a range");
                        vi_range(r2, &a, &z, &down, "an array's range");
                        t.two = 1;
                        t.blo = (int)(a < z ? a : z);
                        t.bhi = (int)(a < z ? z : a);
                    }
                }
                if ((long)(t.ahi - t.alo + 1) * (t.two ? t.bhi - t.blo + 1 : 1) > 4096)
                    vi_fail(r, "Wire handles arrays of up to 4096 elements");
                vi_type(ety, &t.elem, 2);
                if (t.elem.ty == T_REC) vi_fail(ety, "an array of records is not supported yet");
                if (t.elem.ty == T_ARR) {   /* an array of arrays: like an array with two indexes */
                    VType *in = &vi_types[t.elem.ut - 1];
                    if (in->two || t.two || t.unbound) vi_fail(ety, "Wire handles arrays with up to two indexes");
                    t.two = 2;   /* written a(i)(j), not a(i, j) */
                    t.blo = in->alo; t.bhi = in->ahi;
                    t.elem = in->elem;
                    if ((long)(t.ahi - t.alo + 1) * (t.bhi - t.blo + 1) > 4096) vi_fail(r, "Wire handles arrays of up to 4096 elements");
                }
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
                if (proto.ty == T_REC) {   /* a record constant: a constant per field */
                    VType *rt = &vi_types[proto.ut - 1];
                    int f, si;
                    s = vi_declare(id, VK_CONST);
                    si = (int)(s - vi_syms);
                    vi_syms[si].ty = T_REC; vi_syms[si].ut = proto.ut;
                    vi_rec_fields(si, VK_CONST, NULL, 0);
                    for (f = 0; f < rt->nf; f++) {
                        VSym *e = &vi_syms[vi_syms[si].fld0 + f];
                        gram_node *pv = vi_rec_part(val, rt, f);
                        if (e->ty == T_INT) { e->ival = vi_const_int(pv, "a constant's value"); continue; }
                        {
                            VX x = vi_gen(pv, e->w);
                            if (!x.cst) vi_fail(pv, "a constant's value must be known: literals and other constants");
                            e->bits = x.v;
                        }
                    }
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
                    s->intneg = proto.intneg;
                s->intneg = proto.intneg;
                    s->ut = proto.ut;
                    s->bits = x.v;
                }
            }
        } else if (d->rule == VHDL_R_funcdecl || d->rule == VHDL_R_procdecl) {   /* inlined where it is called */
            if (vi_nfuncs == 256) vi_fail(d, "more than 256 functions and procedures");
            vi_fctx[vi_nfuncs] = vi_ctx;
            vi_funcs[vi_nfuncs++] = d;
        } else if (d->rule == VHDL_R_subspec) {
            /* function f(...) return t;  its body comes later (in the package body) */
        } else if (d->rule == VHDL_R_subtype) {
            VType t;
            memset(&t, 0, sizeof t);
            t.id = d->child;
            t.issub = 1;
            t.subnode = d->child->next;
            {   /* read it once here, so a mistake in it is reported here */
                VSym probe;
                memset(&probe, 0, sizeof probe);
                vi_type(t.subnode, &probe, 1);
            }
            vi_types = (VType *)realloc(vi_types, (size_t)(vi_ntypes + 1) * sizeof *vi_types);
            vi_types[vi_ntypes++] = t;
        } else if (d->rule == VHDL_R_alias) {   /* alias x is name: reading x reads the name */
            VSym *s = vi_declare(d->child, VK_SIG);
            gram_node *e;
            for (e = d->child->next; e->next; e = e->next) {}
            s->aliased = e;
            if (d->child->next != e) {   /* alias x : t is ...: x has that type's range */
                VSym proto;
                memset(&proto, 0, sizeof proto);
                vi_type(d->child->next, &proto, 0);
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                s->intneg = proto.intneg;
            } else {   /* the range of what it names */
                VX x = vi_gen_any(e, -1);
                s->ty = x.ty == T_STR ? T_SLV : x.ty; s->w = x.w; s->left = x.w - 1; s->right = 0; s->down = 1;
                if (e->rule == VHDL_R_ident && vi_find(e)) {
                    VSym *o = vi_find(e);
                    s->ty = o->ty; s->left = o->left; s->right = o->right; s->down = o->down; s->ut = o->ut;
                } else if (e->rule == VHDL_R_name && e->child->next && e->child->next->rule == VHDL_R_args
                           && e->child->next->child->rule == VHDL_R_drange) {
                    long a, z;
                    int dn;
                    vi_range(e->child->next->child, &a, &z, &dn, "a slice");
                    s->left = (int)a; s->right = (int)z; s->down = dn;
                }
            }
        } else if (d->rule == VHDL_R_attrdecl) {
            /* attribute x : type; only its uses mean anything */
        } else if (d->rule == VHDL_R_attrspec) {
            gram_node *an = d->child, *ids = an->next, *val = ids->next->next;
            if (vi_is(an, "wire_screen")) {
                long w;
                if (!vi_ceval(val, &w)) vi_fail(val, "wire_screen is the width of the screen in pixels, a number like 16");
                for (id = ids->child; id; id = id->next) {
                    VSym *s = vi_find(id);
                    if (!s) vi_fail(id, "%.*s is not declared above this line", VT(id));
                    if (s->ty != T_ARR || !s->ramproc)
                        vi_fail(id, "Wire draws %.*s as a screen when it is an array written like a ram: one clocked process with "
                                "if we = '1' then %.*s(to_integer(addr)) <= pixel; end if;, read only as %.*s(to_integer(addr))",
                                VT(id), VT(id), VT(id));
                    s->screenw = (int)w;
                }
            } else if (vi_is(an, "wire_cpu")) {   /* the circuit can be programmed: Wire's cpu statement */
                if (val->len < 2 || val->text[0] != '"')
                    vi_fail(val, "wire_cpu is a string: \"isa=name.isa; text=instr; data=ram; pc=pc\"");
                vi_cpu_at = val;
                vi_cpu_spec = vi_fmt("%.*s", (int)val->len - 2, val->text + 1);
            } else if (vi_is(an, "wire_text")) {   /* the screen shows characters */
                for (id = ids->child; id; id = id->next) {
                    VSym *s = vi_find(id);
                    if (!s) vi_fail(id, "%.*s is not declared above this line", VT(id));
                    if (!s->screenw) vi_fail(id, "wire_text goes with wire_screen: give %.*s its width in characters first", VT(id));
                    s->screentext = 1;
                }
            }   /* other attributes (keep, mark_debug, ...) are for the tools */
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
    int ctx = 0, pend[16], npend = 0;
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
                if (!strncmp(low, "work.", 5)) {   /* use work.p.all: package p, declared above */
                    const char *pn = low + 5, *dot = strchr(pn, '.');
                    int n = dot ? (int)(dot - pn) : (int)strlen(pn), q;
                    for (q = 0; q < vi_npkgs; q++)
                        if ((int)vi_pkgs[q].spec->child->len == n && vi_is(vi_pkgs[q].spec->child, vi_fmt("%.*s", n, pn))) break;
                    if (q == vi_npkgs) {
                        if (dot) vi_fail(k, "there is no package called %.*s above this line", n, k->text + 5);
                        continue;   /* use work.entity_name: nothing to declare */
                    }
                    if (npend == 16) vi_fail(k, "more than 16 packages");
                    pend[npend++] = q;
                }
            }
        } else if (u->rule == VHDL_R_package || u->rule == VHDL_R_pkgbody) {
            gram_node *nm = u->child;
            int q;
            for (q = 0; q < vi_npkgs; q++)
                if (vi_pkgs[q].spec->child->len == nm->len && vi_is(vi_pkgs[q].spec->child, vi_fmt("%.*s", VT(nm)))) break;
            if (u->rule == VHDL_R_package) {
                if (q < vi_npkgs) vi_fail(nm, "there are two packages called %.*s", VT(nm));
                if (vi_npkgs == 64) vi_fail(nm, "more than 64 packages");
                memset(&vi_pkgs[vi_npkgs], 0, sizeof vi_pkgs[0]);
                vi_pkgs[vi_npkgs].spec = u;
                vi_pkgs[vi_npkgs].ctx = ctx;
                memcpy(vi_pkgs[vi_npkgs].deps, pend, sizeof pend);
                vi_pkgs[vi_npkgs++].ndeps = npend;
            } else {
                if (q == vi_npkgs) vi_fail(nm, "this is the body of package %.*s, but there is no package called that above it", VT(nm));
                vi_pkgs[q].body = u;
                vi_pkgs[q].ctx |= ctx;
            }
            ctx = 0;
            npend = 0;
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
            memcpy(vi_ents[vi_nents]->pkgs, pend, sizeof pend);
            vi_ents[vi_nents]->npkgs = npend;
            ctx = 0;
            npend = 0;
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

    /* an entity without ports beside others is a testbench (wait for, report ...):
     * for a simulator, not hardware; the design is what it tests */
    {
        int nport = 0;
        for (ln = 0; ln < vi_nbase; ln++) if (vi_kid(vi_ents[ln]->ent, VHDL_R_ports)) nport++;
        if (nport)
            for (ln = 0; ln < vi_nbase; ln++) if (!vi_kid(vi_ents[ln]->ent, VHDL_R_ports)) vi_ents[ln]->tb = 1;
    }
    /* which entities are placed by others; the top is one nobody places (the last, if several) */
    for (ln = 0; ln < vi_nbase; ln++) if (!vi_ents[ln]->tb) vi_mark_used(vi_ents[ln]->arch->child->next->next);
    {
        int t = -1;
        for (ln = 0; ln < vi_nbase; ln++) if (!vi_ents[ln]->used && !vi_ents[ln]->tb) t = ln;
        if (t < 0) vi_fail(vi_ents[vi_nbase - 1]->ent->child, "every entity is used by another one, so none is the top");
        vi_ents[t]->top = 1;
    }
    for (ln = 0; ln < vi_nbase; ln++) {
        VEnt *e = vi_ents[ln];
        if (e->tb) continue;
        e->wname = vi_unique(vi_fmt("%.*s", VT(e->ent->child)));
        vi_ent_ports(e);
    }

    /* the entities on their own lines; then the copies for generic maps (which
     * can make more copies) after the last line */
    for (ln = 0; ln < vi_nbase; ln++) if (!vi_ents[ln]->tb) vi_ent_emit(vi_ents[ln]);
    for (ln = vi_nbase; ln < vi_nents; ln++) vi_ent_emit(vi_ents[ln]);

    for (ln = 1; ln <= vi_nlines; ln++) {
        if (vi_lines[ln].n) vb_add(&out, vi_lines[ln].s, vi_lines[ln].n);
        if (ln < vi_nlines) vb_add(&out, "\n", 1);
    }
    if (vi_extra.n) vb_add(&out, vi_extra.s, vi_extra.n);
    if (vi_pcomp.n) {   /* the processes drawn as chips, after the copies */
        vb_add(&out, vi_pcomp.s, vi_pcomp.n);
        vi_xline = (int *)realloc(vi_xline, (size_t)(vi_nx + vi_npx + 1) * sizeof *vi_xline);
        memcpy(vi_xline + vi_nx, vi_pxline, (size_t)vi_npx * sizeof *vi_xline);
        vi_nx += vi_npx;
    }
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
