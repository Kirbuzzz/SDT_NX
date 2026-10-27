/* SDT loader - hito 2 (DIAGNOSTICO de memoria ejecutable)
 * Objetivo: averiguar como colocar el codigo de una .so (RX) con sus datos (RW)
 * pegados en direcciones contiguas, que es lo que necesitan las librerias de Android.
 * Cada paso se escribe en sdmc:/switch/sdt/log.txt ANTES de ejecutarse, asi si algo
 * se cuelga o cierra el programa, la ultima linea del log dice donde fue. */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <sys/stat.h>

#define DIR_BASE "sdmc:/switch/sdt"
int main(int argc, char **argv);
static FILE *g_log;

static void LOG(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt); vprintf(fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
    consoleUpdate(NULL);
}

static u64 info(u32 id) { u64 v = 0; Result rc = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0); return R_SUCCEEDED(rc) ? v : (u64)-1; }

/* 1) syscalls permitidas y regiones de direcciones */
static void test_env(void) {
    LOG("== 1) Entorno ==\n");
    static const u8 ids[] = { 0x4B, 0x4C, 0x73, 0x74, 0x75, 0x77, 0x78 };
    LOG("Syscalls permitidas:");
    for (unsigned i = 0; i < sizeof ids; i++) LOG(" %02X=%d", ids[i], envIsSyscallHinted(ids[i]) ? 1 : 0);
    LOG("\nHandle de proceso propio: 0x%x\n", (unsigned)envGetOwnProcessHandle());
    LOG("ASLR  : %llx +%llx\n", (unsigned long long)info(12), (unsigned long long)info(13));
    LOG("Alias : %llx +%llx\n", (unsigned long long)info(2),  (unsigned long long)info(3));
    LOG("Heap  : %llx +%llx\n", (unsigned long long)info(4),  (unsigned long long)info(5));
    LOG("Stack : %llx +%llx\n", (unsigned long long)info(14), (unsigned long long)info(15));
    LOG("main()=%p  &LOG=%p\n", (void *)main, (void *)LOG);
    static const size_t mb[] = { 64, 256, 1024, 2048 };
    for (unsigned i = 0; i < 4; i++) {
        void *p = malloc(mb[i] << 20);
        LOG("malloc %zu MB: %s %p\n", mb[i], p ? "ok" : "FALLA", p);
        free(p);
    }
}

/* 2) JIT de libnx (lo que ya funcionaba) */
static void test_jit(void) {
    LOG("\n== 2) JIT de libnx ==\n");
    Jit j;
    Result rc = jitCreate(&j, 0x10000);
    if (R_FAILED(rc)) { LOG("jitCreate fallo rc=0x%x\n", rc); return; }
    u32 *rw = (u32 *)jitGetRwAddr(&j); void *rx = jitGetRxAddr(&j);
    LOG("rw=%p rx=%p\n", (void *)rw, rx);
    jitTransitionToWritable(&j);
    rw[0] = 0xD2800540; rw[1] = 0xD65F03C0;   /* mov x0,#42 ; ret */
    jitTransitionToExecutable(&j);
    long r = ((long (*)(void))rx)();
    LOG("devolvio %ld -> %s\n", r, r == 42 ? "OK" : "FALLO");
    jitClose(&j);
}

/* 3) CLAVE: pagina 0 = codigo RX, pagina 1 = datos RW, contiguas, via svc de proceso */
static void test_split(void) {
    LOG("\n== 3) Codigo RX + datos RW contiguos ==\n");
    Handle proc = envGetOwnProcessHandle();
    if (!proc || !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x78)) {
        LOG("No disponible (faltan syscalls 73/77/78 o el handle de proceso). Fin de la prueba.\n");
        return;
    }
    const size_t sz = 0x4000;
    u8 *src = (u8 *)memalign(0x1000, sz);
    if (!src) { LOG("sin memoria\n"); return; }
    memset(src, 0, sz);
    u32 *code = (u32 *)src;
    code[0] = 0x10008001;  /* adr x1, #0x1000  (-> pagina de datos) */
    code[1] = 0xF9400020;  /* ldr x0, [x1]                          */
    code[2] = 0x91000400;  /* add x0, x0, #1                        */
    code[3] = 0xF9000020;  /* str x0, [x1]                          */
    code[4] = 0xD65F03C0;  /* ret                                   */
    *(u64 *)(src + 0x1000) = 41;
    armDCacheFlush(src, sz);

    virtmemLock();
    void *dst = virtmemFindAslr(sz, 0x1000);
    VirtmemReservation *rv = dst ? virtmemAddReservation(dst, sz) : NULL;
    virtmemUnlock();
    LOG("src=%p dst=%p\n", (void *)src, dst);
    if (!dst) { LOG("no hay hueco de direcciones\n"); return; }

    LOG("svcMapProcessCodeMemory...\n");
    Result rc = svcMapProcessCodeMemory(proc, (u64)dst, (u64)src, sz);
    LOG("  rc=0x%x\n", rc);
    if (R_FAILED(rc)) { if (rv) virtmemRemoveReservation(rv); return; }

    LOG("permiso RX en pagina 0...\n");
    rc = svcSetProcessMemoryPermission(proc, (u64)dst, 0x1000, Perm_Rx);
    LOG("  rc=0x%x\n", rc);
    LOG("permiso RW en pagina 1...\n");
    rc = svcSetProcessMemoryPermission(proc, (u64)dst + 0x1000, 0x1000, Perm_Rw);
    LOG("  rc=0x%x\n", rc);
    armICacheInvalidate(dst, 0x1000);

    LOG("ejecutando codigo (lee y escribe la pagina de datos)...\n");
    long (*fn)(void) = (long (*)(void))dst;
    long r1 = fn();
    LOG("  1a llamada: %ld (esperado 42)\n", r1);
    long r2 = fn();
    LOG("  2a llamada: %ld (esperado 43)\n", r2);
    LOG("RESULTADO: %s\n", (r1 == 42 && r2 == 43) ? "FUNCIONA codigo RX + datos RW contiguos" : "FALLO");

    svcUnmapProcessCodeMemory(proc, (u64)dst, (u64)src, sz);
    if (rv) virtmemRemoveReservation(rv);
    free(src);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    mkdir(DIR_BASE, 0777);
    g_log = fopen(DIR_BASE "/log.txt", "w");
    LOG("SDT loader - hito 2 (diagnostico)\n");
    LOG("applet type: %d\n\n", (int)appletGetAppletType());

    test_env();
    test_jit();
    test_split();

    LOG("\nListo. Log en " DIR_BASE "/log.txt\nPulsa + para salir.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    if (g_log) fclose(g_log);
    consoleExit(NULL);
    return 0;
}
