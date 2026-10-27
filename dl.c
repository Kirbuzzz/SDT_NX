/* Registro de modulos cargados y emulacion de dlopen/dlsym. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdt.h"

typedef struct { char name[48]; ElfModule m; int loaded; int inited; } ModEnt;
static ModEnt g_ents[4];
static char g_sys;                       /* "handle" de las librerias del sistema */
static char g_err[200]; static int g_has_err;

static const char *base_name(const char *p) { const char *b = strrchr(p, '/'); return b ? b + 1 : p; }
static ModEnt *ent_by_name(const char *n) {
    for (int i = 0; i < 4; i++) if (g_ents[i].loaded && !strcmp(g_ents[i].name, n)) return &g_ents[i];
    return NULL;
}
ElfModule *mod_find(const char *name) { ModEnt *e = ent_by_name(name); return e ? &e->m : NULL; }

void *mod_export(const char *sym) {
    for (int i = 0; i < 4; i++) if (g_ents[i].loaded) { void *a = elf_sym(&g_ents[i].m, sym); if (a) return a; }
    return NULL;
}

int mod_load(const char *dir, const char *name, const ElfPlatform *pl) {
    ModEnt *e = NULL;
    for (int i = 0; i < 4; i++) if (!g_ents[i].loaded) { e = &g_ents[i]; break; }
    if (!e) return -20;
    char path[256]; snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("[%s] no encontrado: %s\n", name, path); return -21; }
    fseek(f, 0, SEEK_END); size_t n = ftell(f); rewind(f);
    uint8_t *buf = malloc(n);
    if (!buf) { fclose(f); return -22; }
    if (fread(buf, 1, n, f) != n) { free(buf); fclose(f); return -23; }
    fclose(f);
    int rc = elf_load(&e->m, name, buf, n, pl);
    free(buf);
    if (rc) { LOG("[%s] elf_load ERROR %d\n", name, rc); return rc; }
    rc = elf_commit(&e->m, pl);
    if (rc) { LOG("[%s] elf_commit ERROR %d\n", name, rc); return rc; }
    snprintf(e->name, sizeof e->name, "%s", name);
    e->loaded = 1;
    return 0;
}

void *sdt_dlopen(const char *path, int flags) {
    (void)flags;
    LOG("dlopen(%s)\n", path ? path : "(null)");
    if (!path) return &g_sys;
    const char *b = base_name(path);
    ModEnt *e = ent_by_name(b);
    if (e) {
        if (!e->inited) {
            e->inited = 1;
            LOG("  ejecutando constructores de %s (%zu en init_array)...\n", b, e->m.init_count);
            elf_run_init(&e->m);
            LOG("  constructores de %s ok\n", b);
        }
        return e;
    }
    static const char *const sys[] = { "libc.so", "libm.so", "libdl.so", "liblog.so", "libz.so", "libandroid.so", NULL };
    for (int i = 0; sys[i]; i++) if (!strcmp(b, sys[i])) return &g_sys;
    snprintf(g_err, sizeof g_err, "library \"%s\" not found", path); g_has_err = 1;
    LOG("  (no disponible)\n");
    return NULL;
}

void *sdt_dlsym(void *h, const char *name) {
    void *r = NULL;
    if (!h || h == &g_sys || h == (void *)(intptr_t)-1) { r = shim_lookup(name); if (!r) r = mod_export(name); }
    else for (int i = 0; i < 4; i++) if (h == &g_ents[i]) { r = elf_sym(&g_ents[i].m, name); break; }
    LOG("dlsym(%s) -> %p\n", name, r);
    if (!r) { snprintf(g_err, sizeof g_err, "undefined symbol: %s", name); g_has_err = 1; }
    return r;
}
int sdt_dlclose(void *h) { (void)h; return 0; }
char *sdt_dlerror(void) { if (!g_has_err) return NULL; g_has_err = 0; return g_err; }
