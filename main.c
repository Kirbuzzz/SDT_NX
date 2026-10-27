/* SDT loader - hito 3
 * Carga libmain.so, libil2cpp.so y libunity.so de verdad (codigo RX + datos RW),
 * llama a JNI_OnLoad de libmain.so con un JNI falso y despues a NativeLoader.load(dir),
 * que hace dlopen de libil2cpp.so y libunity.so. Cada paso queda en sdmc:/switch/sdt/log.txt.
 * Cuando el codigo cargado llame a algo que aun no existe, el programa se detiene y
 * escribe su NOMBRE: asi vamos completando shims uno a uno. */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "sdt.h"

#define DIR_BASE "sdmc:/switch/sdt"
#define DIR_LIB  DIR_BASE "/lib"

int g_guest;
static FILE *g_logf, *g_unres;
static PadState g_pad;

/* El codigo de Android lee el "canario" de pila en TPIDR_EL0+0x28. En Horizon esa zona es el
 * buffer de mensajes IPC (lo pisa cualquier E/S de la SD). Lo reescribimos tras cada E/S y
 * antes de entrar en codigo cargado, para que el chequeo de pila no falle. */
#if defined(__aarch64__)
static inline uint8_t *tp(void) { uint64_t x; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(x)); return (uint8_t *)x; }
#else
static inline uint8_t *tp(void) { return NULL; }
#endif
static const uint64_t CANARY = 0x5344544e58434b31ULL;
void canary_fix(void) { uint8_t *t = tp(); if (t) *(volatile uint64_t *)(t + 0x28) = CANARY; }

void LOG(const char *fmt, ...) {
    va_list a;
    if (g_logf) { va_start(a, fmt); vfprintf(g_logf, fmt, a); va_end(a); fflush(g_logf); }
    if (!g_guest) { va_start(a, fmt); vprintf(fmt, a); va_end(a); consoleUpdate(NULL); }
    canary_fix();
}

void fatal_wait(const char *why) {
    g_guest = 0;
    LOG("\n*** DETENIDO: %s ***\nMira " DIR_BASE "/log.txt. Pulsa + para salir.\n", why);
    while (appletMainLoop()) {
        padUpdate(&g_pad);
        if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    if (g_logf) fclose(g_logf);
    consoleExit(NULL);
    exit(0);
}

static void *p_resolve(const char *sym, void *u) {
    (void)u;
    void *a = shim_lookup(sym);
    return a ? a : mod_export(sym);
}
static void *p_stub(const char *sym, void *u) { (void)u; return stub_make(sym); }
static void p_unres(const char *mod, const char *sym, int weak, void *u) {
    (void)u;
    if (g_unres) fprintf(g_unres, "%s\t%s\t%s\n", mod, sym, weak ? "weak" : "strong");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    mkdir(DIR_BASE, 0777); mkdir(DIR_LIB, 0777);
    g_logf = fopen(DIR_BASE "/log.txt", "w");
    g_unres = fopen(DIR_BASE "/unresolved.tsv", "w");
    LOG("SDT loader - hito 3\n");

    ElfPlatform pl; nx_platform(&pl);
    pl.resolve = p_resolve; pl.make_stub = p_stub; pl.unresolved = p_unres;
    jni_init();

    static const char *const libs[] = { "libmain.so", "libil2cpp.so", "libunity.so" };
    for (int i = 0; i < 3; i++) {
        int rc = mod_load(DIR_LIB, libs[i], &pl);
        if (rc) fatal_wait("no se pudo cargar una libreria");
        ElfModule *m = mod_find(libs[i]);
        LOG("[%s] ok: %.1f MB en %p, relocs %zu, stubs %zu, init_array %zu\n",
            libs[i], m->size / 1048576.0, (void *)m->load, m->n_reloc, m->n_unres, m->init_count);
    }
    if (g_unres) { fclose(g_unres); g_unres = NULL; }

    ElfModule *mm = mod_find("libmain.so");
    int32_t (*onload)(void *, void *) = (int32_t (*)(void *, void *))elf_sym(mm, "JNI_OnLoad");
    if (!onload) fatal_wait("libmain.so no exporta JNI_OnLoad");
    LOG("\n-> JNI_OnLoad(libmain.so) en %p\n", (void *)onload);
    g_guest = 1; canary_fix();
    int32_t v = onload(jni_vm(), NULL);
    g_guest = 0;
    LOG("<- JNI_OnLoad devolvio 0x%x (esperado 0x10006)\n", v);

    void *fn = NULL, *cls = NULL;
    if (jni_find_native("com/unity3d/player/NativeLoader", "load", &fn, &cls))
        fatal_wait("libmain no registro NativeLoader.load");
    void *dir = jni_new_string(DIR_LIB);
    LOG("\n-> NativeLoader.load(\"%s\") en %p\n", DIR_LIB, fn);
    g_guest = 1; canary_fix();
    uint8_t ok = ((uint8_t (*)(void *, void *, void *))fn)(jni_env(), cls, dir);
    g_guest = 0;
    LOG("<- NativeLoader.load devolvio %d\n", ok);

    LOG("\nListo. Pulsa + para salir.\n");
    while (appletMainLoop()) {
        padUpdate(&g_pad);
        if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    if (g_logf) fclose(g_logf);
    consoleExit(NULL);
    return 0;
}
