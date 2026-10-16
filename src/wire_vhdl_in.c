/* wire_vhdl_in.c - VHDL in: reads a VHDL file and turns it into Wire, which
 * wire.c then checks, lays out and simulates like any other circuit.
 * Included by wire.c, after wire_vhdl.c (it uses its VBuf).
 *
 *   wire adder.vhd               writes adder.html
 *   wire --to-wire adder.vhd     prints the Wire it was turned into
 *
 * What it reads: one entity and its architecture, with generics (their
 * default values), std_logic, std_logic_vector, unsigned and signed ports
 * and signals, integer and vector constants, and the concurrent statements:
 * assignments, when/else and with/select. Expressions: and or xor nand nor
 * xnor not, = /= < <= > >=, + - (numeric_std, or std_logic_unsigned), &,
 * indexes and slices, aggregates like (others => '0'), the conversions
 * unsigned() signed() std_logic_vector() to_unsigned() to_signed() resize(),
 * shift_left() shift_right() and sll srl sla sra rol ror by a constant.
 * Processes, generate loops and component instances are recognised and
 * refused with a message, not a syntax error.
 *
 * The Wire keeps the VHDL's lines: each statement lands on the line (and
 * near the column) it came from, so every error Wire finds later, and every
 * part's line on the page, points at the right line of the VHDL. */
#include "vhdl_grammar.h"

enum { T_SL, T_BOOL, T_SLV, T_UNS, T_SGN, T_STR, T_INT };
static const char *vi_tyname[] = { "std_logic", "boolean", "std_logic_vector", "unsigned", "signed",
                                   "a literal", "integer" };
enum { VK_IN, VK_OUT, VK_SIG, VK_CONST, VK_GEN };

typedef struct {
    const char *name;            /* as declared (points into the VHDL) */
    int nlen;
    char *wname;                 /* its name in Wire */
    int kind, ty, w;
    int left, right, down;       /* a vector's range: left downto/to right */
    long ival;                   /* an integer constant or generic */
    unsigned long bits;          /* a vector constant */
    const gram_node *at;
} VSym;

/* an expression, as Wire text, with what VHDL knows about it */
typedef struct {
    char *s;
    int ty, w;
    int atom;                    /* 2: a name, call or {...} (can be sliced); 1: ~x; 0: a & b */
    int cst;                     /* the value is known: v (bits) or iv (integer) */
    unsigned long v;
    long iv;
    int cat;                     /* s is a {...} that more can be added to */
} VX;

static const char *vi_src;       /* the VHDL */
static size_t vi_len;
static const char *vi_file;
static VSym *vi_syms;
static int vi_nsyms, vi_cap;
static int vi_slv_num;           /* 1: std_logic_unsigned is used, 2: std_logic_signed */
static VBuf *vi_lines;           /* the Wire, one buffer per line of the VHDL */
static int vi_nlines;

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

static VSym *vi_find(const gram_node *id)
{
    int i, j;
    for (i = 0; i < vi_nsyms; i++) {
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
    if (vi_find(id)) vi_fail(id, "%.*s is declared twice", VT(id));
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
    if (kind != VK_CONST && kind != VK_GEN) {
        char *w = vi_fmt("%.*s", VT(id));
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

static void vi_emit(const gram_node *at, const char *f, ...)
{
    va_list ap;
    size_t line = 1, col = 1;
    char tmp[256], *s;
    int n;
    VBuf *b;
    gram_locate(vi_src, vi_len, (size_t)(at->text - vi_src), &line, &col);
    if ((int)line > vi_nlines) line = (size_t)vi_nlines;
    b = &vi_lines[line];
    if (b->n == 0) { size_t i; for (i = 1; i < col; i++) vb_add(b, " ", 1); }
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
static void vi_type(gram_node *t, VSym *s, int allow_int)
{
    gram_node *id = t->child, *r = id->next;
    if (vi_is(id, "std_logic") || vi_is(id, "std_ulogic") || vi_is(id, "bit")) {
        if (r) vi_fail(r, "%.*s is one bit and has no range", VT(id));
        s->ty = T_SL; s->w = 1;
        return;
    }
    if (vi_is(id, "integer") || vi_is(id, "natural") || vi_is(id, "positive")) {
        if (!allow_int)
            vi_fail(id, "Wire needs a width here: use std_logic_vector(n-1 downto 0) or unsigned(n-1 downto 0) "
                        "instead of %.*s", VT(id));
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
    if (s->w > MAXW) vi_fail(r, "Wire handles up to %d bits, and this is %d", MAXW, s->w);
}

static int vi_num(int ty)        /* can do arithmetic */
{
    return ty == T_UNS || ty == T_SGN || (ty == T_SLV && vi_slv_num);
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
    if (w > MAXW) vi_fail(n, "Wire handles up to %d bits, and this is %d", MAXW, w);
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
    if (bits > MAXW) vi_fail(n, "Wire handles up to %d bits, and this is %d", MAXW, bits);
    return vi_const(v, bits, T_STR);
}

/* ---- expressions ----------------------------------------------------------- */

static VX vi_gen(gram_node *n, int want);

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
        if (!vi_ceval(arg, &i))
            vi_fail(arg, "picking a bit with a signal is not supported yet; the index must be a constant");
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
    if (args->next) vi_fail(args->next, "one index or slice at a time, please");
    if (s && s->kind != VK_GEN && !(s->kind == VK_CONST && s->ty == T_INT)) {
        if (vi_nargs(args) != 1) vi_fail(args, "%.*s takes one index or slice", VT(id));
        return vi_index(s, id, args->child);
    }
    if (s) vi_fail(id, "%.*s is an integer; it has no bits to pick", VT(id));
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
        if (v.ty != T_INT || !v.cst)
            vi_fail(vi_arg(args, 0), "integer signals are not supported yet: %.*s needs a constant here", VT(id));
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
        if (!vi_ceval(vi_arg(args, 1), &k))
            vi_fail(vi_arg(args, 1), "shifting by a signal (a barrel shifter) is not supported yet; shift by a constant");
        return vi_shift(x, vi_is(id, "shift_left") || vi_is(id, "rotate_left"), x.ty == T_SGN,
                        vi_is(id, "rotate_left") || vi_is(id, "rotate_right"), k, n);
    }
    if (vi_is(id, "to_integer") || vi_is(id, "conv_integer"))
        vi_fail(id, "integer signals are not supported yet (here, %.*s() with a signal); keep the value unsigned", VT(id));
    if (vi_is(id, "rising_edge") || vi_is(id, "falling_edge"))
        vi_fail(id, "%.*s belongs in a clocked process, and processes are not supported yet", VT(id));
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
    if (w < 1) {
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
    if (a->ty == T_INT && b->ty == T_INT) return;
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
            if (acc.w + b.w > MAXW) vi_fail(k, "Wire handles up to %d bits, and this is %d", MAXW, acc.w + b.w);
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
            if (!sub) acc = vi_mk(vi_fmt("add(%s, %s)", acc.s, b.s), acc.ty, acc.w, 2);
            else if (b.cst) {
                if (b.v == 0) continue;
                acc = vi_mk(vi_fmt("add(%s, %s)", acc.s, vi_lit((~b.v + 1) & vi_mask(b.w), b.w)), acc.ty, acc.w, 2);
            } else acc = vi_mk(vi_fmt("add(%s, %s, cin = 1)", acc.s, vi_not(b).s), acc.ty, acc.w, 2);
        }
    }
    return acc;
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
            if (vi_is(n, "true") || vi_is(n, "false")) return vi_const(vi_is(n, "true"), 1, T_BOOL);
            vi_fail(n, "there is no signal, port or constant called %.*s", VT(n));
        }
        if (s->ty == T_INT) { x.ty = T_INT; x.cst = 1; x.iv = s->ival; x.s = vi_fmt("%ld", s->ival); x.atom = 2; return x; }
        if (s->kind == VK_CONST) return vi_const(s->bits, s->w, s->ty);
        return vi_mk(s->wname, s->ty, s->w, 2);
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
        if (!vi_ceval(op->next, &k))
            vi_fail(op->next, "shifting by a signal (a barrel shifter) is not supported yet; shift by a constant");
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

typedef struct { char *s; int ty, w; VSym *sym; } VTarget;

static VTarget vi_target(gram_node *t)
{
    VTarget r;
    gram_node *id = t->child;
    VSym *s = vi_find(id);
    if (!s) vi_fail(id, "there is no signal or port called %.*s", VT(id));
    if (s->kind == VK_IN) vi_fail(id, "%.*s is an input of the entity, so it cannot be given a value here", VT(id));
    if (s->kind == VK_CONST || s->kind == VK_GEN) vi_fail(id, "%.*s is a constant", VT(id));
    r.sym = s;
    if (!id->next) { r.s = s->wname; r.ty = s->ty; r.w = s->w; return r; }
    {
        VX x = vi_index(s, id, id->next);
        r.s = x.s; r.ty = x.ty; r.w = x.w;
    }
    return r;
}

/* can value x be given to the target? */
static void vi_assignable(const VTarget *t, VX *x, gram_node *at)
{
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
    if (d0.w == 1 && d0.cst && d1.cst) {         /* '1' when c else '0' is c */
        if (d1.v == 1 && d0.v == 0) { r = c; r.ty = d0.ty == T_STR ? T_SL : d0.ty; return r; }
        if (d1.v == 0 && d0.v == 1) { r = vi_not(c); r.ty = d0.ty == T_STR ? T_SL : d0.ty; return r; }
    }
    return vi_mk(vi_fmt("mux(%s, %s, sel = %s)", d0.s, d1.s, c.s), d1.ty == T_STR ? d0.ty : d1.ty, d0.w, 2);
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

static void vi_statement(gram_node *st)
{
    switch (st->rule) {
    case VHDL_R_condassign: vi_condassign(st); break;
    case VHDL_R_selassign: vi_selassign(st); break;
    case VHDL_R_assert: break;   /* only for simulation */
    case VHDL_R_process:
        vi_fail(vi_kid(st, VHDL_R_PROCESS) ? vi_kid(st, VHDL_R_PROCESS) : st,
                "processes are not supported yet. For now Wire reads concurrent VHDL: assignments (<=), "
                "when ... else and with ... select");
        break;
    case VHDL_R_generate:
        vi_fail(st->child->next, "generate is not supported yet; write the statements out one by one");
        break;
    case VHDL_R_inst:
        vi_fail(st, "component instances (port map) are not supported yet; put everything in one entity");
        break;
    default:
        vi_fail(st, "this kind of statement is not supported yet");
    }
}

/* ---- the design ---------------------------------------------------------------- */

static void vi_ports(gram_node *ports, int is_generic)
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
                s->ty = T_INT;
                s->ival = vi_const_int(def, "a generic's default");
            }
            continue;
        }
        if (mode && (vi_is(mode, "inout") || vi_is(mode, "linkage")))
            vi_fail(mode, "%.*s ports are not supported yet; use in or out", VT(mode));
        out = mode && (vi_is(mode, "out") || vi_is(mode, "buffer"));
        vi_type(ty, &proto, 0);
        for (id = ids->child; id; id = id->next) {
            VSym *s = vi_declare(id, out ? VK_OUT : VK_IN);
            s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
            if (s->w > 1 || vi_vec(s->ty)) vbf(&b, "%s%s[%d]", b.n ? ", " : "", s->wname, s->w);
            else vbf(&b, "%s%s", b.n ? ", " : "", s->wname);
        }
        vi_emit(ids, "%s %s", out ? "output" : "input", b.s);
    }
}

static void vi_decls(gram_node *arch)
{
    gram_node *d;
    for (d = arch->child->next->next; d; d = d->next) {
        gram_node *id;
        VSym proto;
        if (d->rule == VHDL_R_signal) {
            VBuf b = { 0, 0, 0 };
            memset(&proto, 0, sizeof proto);
            vi_type(vi_kid(d, VHDL_R_type), &proto, 0);
            for (id = d->child->child; id; id = id->next) {
                VSym *s = vi_declare(id, VK_SIG);
                s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                if (vi_vec(s->ty)) vbf(&b, "%s%s[%d]", b.n ? ", " : "", s->wname, s->w);
                else vbf(&b, "%s%s", b.n ? ", " : "", s->wname);
            }
            vi_emit(d->child, "wire %s", b.s);
        } else if (d->rule == VHDL_R_constant) {
            gram_node *ty = vi_kid(d, VHDL_R_type), *val = ty->next;
            memset(&proto, 0, sizeof proto);
            vi_type(ty, &proto, 1);
            for (id = d->child->child; id; id = id->next) {
                VSym *s;
                if (proto.ty == T_INT) {
                    long v = vi_const_int(val, "a constant's value");
                    s = vi_declare(id, VK_CONST);
                    s->ty = T_INT; s->ival = v;
                } else {
                    VTarget t;
                    VX x;
                    t.s = vi_fmt("%.*s", VT(id)); t.ty = proto.ty; t.w = proto.w; t.sym = NULL;
                    x = vi_gen(val, proto.w);
                    if (!x.cst) vi_fail(val, "a constant's value must be known: literals and other constants");
                    vi_assignable(&t, &x, val);
                    s = vi_declare(id, VK_CONST);
                    s->ty = proto.ty; s->w = proto.w; s->left = proto.left; s->right = proto.right; s->down = proto.down;
                    s->bits = x.v;
                }
            }
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
        if (!ops && vi_operator(x)) continue;
        char low[32];
        int j, k;
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

/* the Wire, on the VHDL's lines; *len gets its length */
static char *vhdl_to_wire(const char *text, size_t len, const char *file, size_t *outlen)
{
    gram_arena a;
    gram_error err;
    gram_grammar *g;
    gram_node *design, *u, *ent = NULL, *arch = NULL, *k;
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
                if (strstr(low, "std_logic_unsigned")) vi_slv_num = 1;
                if (strstr(low, "std_logic_signed")) vi_slv_num = 2;
            }
        } else if (u->rule == VHDL_R_entity) {
            if (ent) vi_fail(u->child, "a second entity (%.*s) is not supported yet: Wire reads one entity per file "
                                       "for now", VT(u->child));
            ent = u;
        } else if (u->rule == VHDL_R_arch) {
            if (arch) vi_fail(u->child, "a second architecture is not supported yet: Wire reads one per file for now");
            arch = u;
        }
    }
    if (!ent) vi_fail(NULL, "there is no entity in this file");
    if (!arch) vi_fail(ent->child, "%.*s has no architecture", VT(ent->child));
    if (!vi_is(arch->child->next, vi_fmt("%.*s", VT(ent->child))))
        vi_fail(arch->child->next, "this architecture is of %.*s, but the entity is %.*s", VT(arch->child->next), VT(ent->child));
    {
        gram_node *last = NULL;
        for (k = ent->child->next; k; k = k->next) last = k;
        if (last && last->rule == VHDL_R_ident && !vi_is(last, vi_fmt("%.*s", VT(ent->child))))
            vi_fail(last, "the entity is called %.*s, but it ends with end %.*s", VT(ent->child), VT(last));
    }

    vi_emit(ent, "circuit \"%.*s\"", VT(ent->child));
    vi_ports(vi_kid(ent, VHDL_R_generics), 1);
    vi_ports(vi_kid(ent, VHDL_R_ports), 0);
    vi_decls(arch);
    for (k = arch->child->next->next; k; k = k->next) {
        if (k->rule == VHDL_R_signal || k->rule == VHDL_R_constant || k->rule == VHDL_R_compdecl
            || k->rule == VHDL_R_other || k->rule == VHDL_R_ident)
            continue;
        vi_statement(k);
    }

    for (ln = 1; ln <= vi_nlines; ln++) {
        if (vi_lines[ln].n) vb_add(&out, vi_lines[ln].s, vi_lines[ln].n);
        if (ln < vi_nlines) vb_add(&out, "\n", 1);
    }
    if (!out.s) vb_add(&out, "", 0);
    free(buf);
    *outlen = out.n;
    return out.s;
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
