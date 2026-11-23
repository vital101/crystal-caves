/* Native port of wasm/persist.js: file-backed config / high scores / save
 * slots in the SDL pref directory, same formats as the browser localStorage
 * keys.  Includes a minimal JSON reader/writer for the two fixed schemas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "persist.h"
#include "objects.h"
#include "shell.h"

#define HS_COUNT     10
#define HS_NAME_MAX  20

static char g_dir[1024];

static const int DEFAULT_KEYS[4] = { 0x4B, 0x4D, 0x1D, 0x38 }; /* L R Ctrl Alt */

void cc_persist_init(void) {
    char *p = SDL_GetPrefPath("re-cycledair", "crystal-caves");
    if (p) {
        snprintf(g_dir, sizeof(g_dir), "%s", p);
        SDL_free(p);
    } else {
        snprintf(g_dir, sizeof(g_dir), ".");
    }
}

/* ---------------------------------------------------------------- files */

static char *read_file(const char *name, long *out_len) {
    char path[1200];
    snprintf(path, sizeof(path), "%s%s", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)) {
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);
    buf[n] = 0;
    *out_len = n;
    return buf;
}

static int write_file(const char *name, const char *data, long len) {
    char path[1200];
    snprintf(path, sizeof(path), "%s%s", g_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = len <= 0 || fwrite(data, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    return ok;
}

/* ------------------------------------------------------------ mini JSON */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;
typedef struct jval {
    jtype t;
    double num;
    int b;
    char *str;
    struct jval **items;   /* ARR elements or OBJ values */
    char **keys;           /* OBJ keys */
    int n;
} jval;

static void jfree(jval *v) {
    if (!v) return;
    if (v->t == J_STR) free(v->str);
    for (int i = 0; i < v->n; i++) {
        jfree(v->items ? v->items[i] : 0);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

typedef struct { const char *p; int err; } jp;

static void jskip(jp *j) { while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++; }

static jval *jparse(jp *j);

static char *jparse_str(jp *j) {
    if (*j->p != '"') { j->err = 1; return 0; }
    j->p++;
    size_t cap = 16, n = 0;
    char *s = malloc(cap);
    while (*j->p && *j->p != '"') {
        unsigned c;
        if (*j->p == '\\') {
            j->p++;
            switch (*j->p) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'n': c = 10; break;
            case 'r': c = 13; break;
            case 't': c = 9; break;
            case 'u': {
                if (!j->p[1] || !j->p[2] || !j->p[3] || !j->p[4]) { j->err = 1; free(s); return 0; }
                c = 0;
                for (int i = 1; i <= 4; i++) {
                    char h = j->p[i];
                    c = c * 16 + (h <= '9' ? h - '0' : (h <= 'F' ? h - 'A' + 10 : h - 'a' + 10));
                }
                j->p += 4;
                break;
            }
            default: j->err = 1; free(s); return 0;
            }
            j->p++;
        } else {
            c = (unsigned char)*j->p++;
        }
        if (n + 5 >= cap) { cap *= 2; s = realloc(s, cap); }
        if (c < 0x80) s[n++] = (char)c;
        else if (c < 0x800) { s[n++] = (char)(0xC0 | (c >> 6)); s[n++] = (char)(0x80 | (c & 63)); }
        else { s[n++] = (char)(0xE0 | (c >> 12)); s[n++] = (char)(0x80 | ((c >> 6) & 63)); s[n++] = (char)(0x80 | (c & 63)); }
    }
    if (*j->p != '"') { j->err = 1; free(s); return 0; }
    j->p++;
    s[n] = 0;
    return s;
}

static jval *jnew(jtype t) {
    jval *v = calloc(1, sizeof(*v));
    if (v) v->t = t;
    return v;
}

static jval *jparse(jp *j) {
    jskip(j);
    char c = *j->p;
    if (c == '{') {
        j->p++;
        jval *v = jnew(J_OBJ);
        jskip(j);
        if (*j->p == '}') { j->p++; return v; }
        while (!j->err) {
            jskip(j);
            char *k = jparse_str(j);
            if (j->err) { free(k); jfree(v); return 0; }
            jskip(j);
            if (*j->p != ':') { j->err = 1; free(k); jfree(v); return 0; }
            j->p++;
            jval *item = jparse(j);
            if (j->err) { free(k); jfree(item); jfree(v); return 0; }
            v->keys = realloc(v->keys, (v->n + 1) * sizeof(char *));
            v->items = realloc(v->items, (v->n + 1) * sizeof(jval *));
            v->keys[v->n] = k;
            v->items[v->n] = item;
            v->n++;
            jskip(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == '}') { j->p++; return v; }
            j->err = 1;
        }
        jfree(v);
        return 0;
    }
    if (c == '[') {
        j->p++;
        jval *v = jnew(J_ARR);
        jskip(j);
        if (*j->p == ']') { j->p++; return v; }
        while (!j->err) {
            jval *item = jparse(j);
            if (j->err) { jfree(item); jfree(v); return 0; }
            v->items = realloc(v->items, (v->n + 1) * sizeof(jval *));
            v->items[v->n++] = item;
            jskip(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == ']') { j->p++; return v; }
            j->err = 1;
        }
        jfree(v);
        return 0;
    }
    if (c == '"') {
        char *s = jparse_str(j);
        if (j->err) { free(s); return 0; }
        jval *v = jnew(J_STR);
        v->str = s;
        return v;
    }
    if (!strncmp(j->p, "true", 4))  { j->p += 4; jval *v = jnew(J_BOOL); v->b = 1; return v; }
    if (!strncmp(j->p, "false", 5)) { j->p += 5; jval *v = jnew(J_BOOL); v->b = 0; return v; }
    if (!strncmp(j->p, "null", 4))  { j->p += 4; return jnew(J_NULL); }
    char *end;
    double d = strtod(j->p, &end);
    if (end == j->p) { j->err = 1; return 0; }
    j->p = end;
    jval *v = jnew(J_NUM);
    v->num = d;
    return v;
}

static jval *json_parse(const char *text) {
    jp j = { text, 0 };
    jval *v = jparse(&j);
    if (j.err) { jfree(v); return 0; }
    return v;
}

static jval *jobj_get(jval *o, const char *key) {
    if (!o || o->t != J_OBJ) return 0;
    for (int i = 0; i < o->n; i++)
        if (!strcmp(o->keys[i], key)) return o->items[i];
    return 0;
}

/* ------------------------------------------------------------- base64 */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64encode(const unsigned char *data, long len, long *out_len) {
    long n = ((len + 2) / 3) * 4;
    char *s = malloc((size_t)n + 1);
    long o = 0;
    for (long i = 0; i < len; i += 3) {
        unsigned v = (unsigned)data[i] << 16;
        if (i + 1 < len) v |= (unsigned)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        s[o++] = B64[(v >> 18) & 63];
        s[o++] = B64[(v >> 12) & 63];
        s[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        s[o++] = i + 2 < len ? B64[v & 63] : '=';
    }
    s[o] = 0;
    *out_len = o;
    return s;
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *b64decode(const char *s, long *out_len) {
    long n = strlen(s);
    unsigned char *out = malloc((size_t)n / 4 * 3 + 3);
    long o = 0;
    for (long i = 0; i + 3 < n + 1 && i < n; i += 4) {
        int a = b64val(s[i]), b = b64val(s[i + 1]);
        int c = i + 2 < n && s[i + 2] != '=' ? b64val(s[i + 2]) : -1;
        int d = i + 3 < n && s[i + 3] != '=' ? b64val(s[i + 3]) : -1;
        if (a < 0 || b < 0) { free(out); return 0; }
        unsigned v = ((unsigned)a << 18) | ((unsigned)b << 12) |
                     (c >= 0 ? (unsigned)c << 6 : 0) | (d >= 0 ? (unsigned)d : 0);
        out[o++] = (unsigned char)(v >> 16);
        if (c >= 0) out[o++] = (unsigned char)(v >> 8);
        if (d >= 0) out[o++] = (unsigned char)v;
    }
    *out_len = o;
    return out;
}

/* ------------------------------------------------------------- config */

static int is_int(jval *v, long lo, long hi) {
    return v && v->t == J_NUM && v->num == (double)(long)v->num &&
           v->num >= lo && v->num <= hi;
}

static int bool01(jval *v, int dflt) {
    if (!v) return dflt;
    if (v->t == J_BOOL) return v->b;
    if (v->t == J_NUM && (v->num == 0 || v->num == 1)) return (int)v->num;
    return dflt;
}

static cc_config default_config(void) {
    cc_config c = { 2, 1, 0, 0, { 0x4B, 0x4D, 0x1D, 0x38 } };
    memcpy(c.keys, DEFAULT_KEYS, sizeof(c.keys));
    return c;
}

static cc_config normalize_config(jval *raw) {
    cc_config c = default_config();
    if (!raw || raw->t != J_OBJ) return c;
    jval *ver = jobj_get(raw, "version");
    if (ver && ver->t == J_NUM && ver->num != 2) return default_config();
    c.sound    = bool01(jobj_get(raw, "sound"), c.sound);
    c.joystick = bool01(jobj_get(raw, "joystick"), c.joystick);
    c.video    = bool01(jobj_get(raw, "video"), c.video);
    jval *keys = jobj_get(raw, "keys");
    if (keys && keys->t == J_ARR && keys->n == 4) {
        int ok = 1;
        for (int i = 0; i < 4; i++)
            if (!is_int(keys->items[i], 0, 0x7F)) { ok = 0; break; }
        if (ok)
            for (int i = 0; i < 4; i++) c.keys[i] = (int)keys->items[i]->num;
    }
    return c;
}

cc_config cc_load_config(void) {
    long n;
    char *text = read_file("cc1.config.v2.json", &n);
    if (text) {
        jval *raw = json_parse(text);
        free(text);
        cc_config c = normalize_config(raw);
        jfree(raw);
        return c;
    }
    /* Legacy v1: toggles preserved, DOS default bindings. */
    text = read_file("cc1.config.v1.json", &n);
    if (text) {
        jval *o = json_parse(text);
        free(text);
        cc_config c = default_config();
        if (o && o->t == J_OBJ) {
            c.sound    = bool01(jobj_get(o, "sound"), c.sound);
            c.joystick = bool01(jobj_get(o, "joystick"), c.joystick);
            c.video    = bool01(jobj_get(o, "video"), c.video);
        }
        jfree(o);
        return c;
    }
    return default_config();
}

int cc_save_config(const cc_config *cfg) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"version\":%d,\"sound\":%d,\"joystick\":%d,\"video\":%d,"
        "\"keys\":[%d,%d,%d,%d]}",
        cfg->version, cfg->sound, cfg->joystick, cfg->video,
        cfg->keys[0], cfg->keys[1], cfg->keys[2], cfg->keys[3]);
    return write_file("cc1.config.v2.json", buf, n);
}

/* ----------------------------------------------------------- hiscores */

static void sanitize_name(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < cap && n < HS_NAME_MAX; p++)
        if (*p >= 0x20 && *p <= 0x7E) out[n++] = *p;
    out[n] = 0;
}

static void json_escape_into(char **o, const char *s) {
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') { *(*o)++ = '\\'; *(*o)++ = c; }
        else if ((unsigned char)c < 0x20) *(*o)++ = ' ';
        else *(*o)++ = c;
    }
}

static void read_name(int i, char *buf, size_t cap) {
    const char *p = cc_shell_hiscore_name(i);
    size_t n = 0;
    for (; p && p[n] && n + 1 < cap; n++) buf[n] = p[n];
    buf[n] = 0;
}

int cc_save_hiscores(void) {
    char *buf = malloc(HS_COUNT * 80 + 16);
    char *o = buf;
    *o++ = '[';
    for (int i = 0; i < HS_COUNT; i++) {
        char name[64];
        read_name(i, name, sizeof(name));
        char clean[HS_NAME_MAX + 1];
        sanitize_name(name, clean, sizeof(clean));
        if (i) *o++ = ',';
        memcpy(o, "{\"name\":\"", 9); o += 9;
        json_escape_into(&o, clean);
        o += sprintf(o, "\",\"score\":%u}", cc_shell_hiscore_score(i));
    }
    *o++ = ']';
    *o = 0;
    int ok = write_file("cc1.hiscores.v1.json", buf, (long)(o - buf));
    free(buf);
    return ok;
}

int cc_load_hiscores(void) {
    long n;
    char *text = read_file("cc1.hiscores.v1.json", &n);
    jval *arr = text ? json_parse(text) : 0;
    free(text);
    int ok = arr && arr->t == J_ARR && arr->n == HS_COUNT;
    if (ok) {
        for (int i = 0; i < HS_COUNT; i++) {
            jval *row = arr->items[i];
            jval *score = jobj_get(row, "score");
            jval *name = jobj_get(row, "name");
            if (!row || row->t != J_OBJ || !is_int(score, 0, 0xFFFFFFFF) ||
                !name || name->t != J_STR) { ok = 0; break; }
            char clean[HS_NAME_MAX + 1];
            sanitize_name(name->str, clean, sizeof(clean));
            cc_shell_hiscore_set(i, clean, (unsigned)(long)score->num);
        }
    }
    if (!ok) {
        cc_save_hiscores();   /* CC1.HS was all-or-nothing */
        jfree(arr);
        return 0;
    }
    jfree(arr);
    return 1;
}

/* ------------------------------------------------------------ save slots */

int cc_save_slot(int slot, unsigned char *stateP, int stateSize) {
    if (slot < 0 || slot > 9 || !stateP || stateSize <= 0) return 0;
    int n = cc_state_save(stateP, stateSize);
    if (!(n > 0 && n <= stateSize)) return 0;
    long blen;
    char *b64 = b64encode(stateP, n, &blen);
    char name[64];
    snprintf(name, sizeof(name), "cc1.save.%d.b64", slot);
    int ok = write_file(name, b64, blen);
    free(b64);
    return ok;
}

int cc_load_slot(int slot, unsigned char *stateP, int stateSize, int hubOnly) {
    if (slot < 0 || slot > 9 || !stateP || stateSize <= 0) return 0;
    char name[64];
    snprintf(name, sizeof(name), "cc1.save.%d.b64", slot);
    long n;
    char *b64 = read_file(name, &n);
    if (!b64 || n <= 0) { free(b64); return 0; }
    long blen;
    unsigned char *bytes = b64decode(b64, &blen);
    free(b64);
    if (!bytes || blen <= 0) { free(bytes); return 0; }
    /* Check before touching state: a corrupt/foreign slot must never
     * overwrite allocations past the state buffer. */
    if (blen > stateSize) { free(bytes); return 0; }
    memcpy(stateP, bytes, (size_t)blen);
    free(bytes);
    return cc_state_load_ex(stateP, (int)blen, hubOnly ? CC_STATE_LOAD_HUB_ONLY : 0) ? 1 : 0;
}
