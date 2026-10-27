/* SDT loader - hito 1 (solo diagnostico, NO arranca el juego todavia)
 *  1) Informa de memoria/modo applet y prueba si hay memoria ejecutable (JIT).
 *  2) Carga libmain.so, libil2cpp.so y libunity.so desde la SD, aplica relocaciones
 *     y lista los simbolos que faltan por emular.
 * Todo se escribe tambien en sdmc:/switch/sdt/log.txt */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <sys/stat.h>
#include "elfload.h"

#define DIR_BASE "sdmc:/switch/sdt"
#define DIR_LIB  DIR_BASE "/lib"

static FILE *g_log, *g_unres;

static void LOG(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt); vprintf(fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
    consoleUpdate(NULL);
}

/* ---- plataforma para elfload (hito 1: memoria normal, sin ejecutar) ---- */
static ElfModule g_mods[4]; static int g_nmods;

static void *p_alloc(size_t size, size_t align) {
    void *p = memalign(align, size);
    if (p) memset(p, 0, size);
    return p;
}
static int p_protect(void *a, size_t l, int prot) { (void)a; (void)l; (void)prot; return 0; } /* hito 2: JIT */
static void *p_resolve(const char *sym, void *u) {
    (void)u;
    for (int i = 0; i < g_nmods; i++) { void *a = elf_sym(&g_mods[i], sym); if (a) return a; }
    return NULL;   /* hito 2: aqui iran los shims (libc, pthread, EGL, JNI...) */
}
static void p_unres(const char *mod, const char *sym, int weak, void *u) {
    (void)u;
    if (g_unres) fprintf(g_unres, "%s\t%s\t%s\n", mod, sym, weak ? "weak" : "strong");
}
static void stub_unresolved(void) { /* si algo llega aqui, falta un shim */ }

/* ---- prueba de memoria ejecutable (JIT) ---- */
static void test_jit(void) {
    Jit j;
    Result rc = jitCreate(&j, 0x10000);
    if (R_FAILED(rc)) { LOG("JIT: jitCreate fallo, rc=0x%x (sin memoria ejecutable en este modo)\n", rc); return; }
    LOG("JIT: jitCreate ok\n");
    u32 *rw = (u32 *)jitGetRwAddr(&j);
    void *rx = jitGetRxAddr(&j);
    jitTransitionToWritable(&j);
    rw[0] = 0xD2800540;   /* mov x0, #42 */
    rw[1] = 0xD65F03C0;   /* ret         */
    jitTransitionToExecutable(&j);
    LOG("JIT: llamando a codigo generado...\n");
    long r = ((long (*)(void))rx)();
    LOG("JIT: devolvio %ld (esperado 42) -> %s\n", r, r == 42 ? "EJECUCION OK" : "FALLO");
    jitClose(&j);
}

static int load_lib(const char *name) {
    char path[256]; snprintf(path, sizeof path, DIR_LIB "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("[%s] no encontrado en %s\n", name, path); return -1; }
    fseek(f, 0, SEEK_END); size_t n = ftell(f); rewind(f);
    uint8_t *buf = malloc(n);
    if (!buf) { LOG("[%s] sin memoria para leer %zu bytes\n", name, n); fclose(f); return -2; }
    if (fread(buf, 1, n, f) != n) { LOG("[%s] error de lectura\n", name); free(buf); fclose(f); return -3; }
    fclose(f);

    ElfPlatform pl = { p_alloc, p_protect, p_resolve, p_unres, NULL, (void *)stub_unresolved };
    ElfModule *m = &g_mods[g_nmods];
    int rc = elf_load(m, name, buf, n, &pl);
    free(buf);
    if (rc) { LOG("[%s] elf_load ERROR %d\n", name, rc); return rc; }
    g_nmods++;
    LOG("[%s] ok: imagen %.1f MB, relocs %zu, sin resolver %zu, init_array %zu\n",
        name, m->size / 1048576.0, m->n_reloc, m->n_unres, m->init_count);
    void *j = elf_sym(m, "JNI_OnLoad");
    if (j) LOG("      JNI_OnLoad en +0x%lx\n", (unsigned long)((uint8_t *)j - m->base));
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);

    mkdir(DIR_BASE, 0777); mkdir(DIR_LIB, 0777);
    g_log = fopen(DIR_BASE "/log.txt", "w");
    g_unres = fopen(DIR_BASE "/unresolved.tsv", "w");
    LOG("SDT loader - hito 1\n");

    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    LOG("Memoria del proceso: total %llu MB, usada %llu MB\n",
        (unsigned long long)(total >> 20), (unsigned long long)(used >> 20));
    LOG("Tipo de applet: %d\n\n", (int)appletGetAppletType());

    test_jit();
    LOG("\n");

    const char *libs[] = { "libmain.so", "libil2cpp.so", "libunity.so" };
    for (int i = 0; i < 3; i++) if (load_lib(libs[i])) break;

    LOG("\nListo. Log en " DIR_BASE "/log.txt\nPulsa + para salir.\n");
    if (g_unres) fclose(g_unres);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    if (g_log) fclose(g_log);
    consoleExit(NULL);
    return 0;
}
