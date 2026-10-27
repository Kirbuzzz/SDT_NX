/* Plataforma Switch para elfload: staging en heap, mapeo con svcMapProcessCodeMemory
 * y permisos por pagina con svcSetProcessMemoryPermission (probado en el hito 2). */
#include <switch.h>
#include <malloc.h>
#include <string.h>
#include "sdt.h"

static size_t up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static void *nx_alloc(size_t size, size_t align) {
    if (align < 0x1000) align = 0x1000;
    void *p = memalign(align, size);
    if (p) memset(p, 0, size);
    return p;
}
static void *nx_reserve(size_t size, size_t align) {
    if (align < 0x1000) align = 0x1000;
    virtmemLock();
    void *a = virtmemFindAslr(size + align, 0x1000);
    void *r = NULL;
    if (a) { r = (void *)up((uintptr_t)a, align); virtmemAddReservation(r, size); }
    virtmemUnlock();
    return r;
}
static int nx_map(void *dst, void *src, size_t size) {
    armDCacheFlush(src, size);
    Result rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)dst, (u64)src, size);
    if (R_FAILED(rc)) { LOG("svcMapProcessCodeMemory fallo dst=%p src=%p size=%zx rc=0x%x\n", dst, src, size, rc); return -1; }
    return 0;
}
static int nx_protect(void *addr, size_t len, int prot) {
    Result rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)addr, len, (u32)prot);
    if (R_FAILED(rc)) { LOG("permiso %d fallo en %p +%zx rc=0x%x\n", prot, addr, len, rc); return -1; }
    if (prot & ELF_PROT_X) armICacheInvalidate(addr, len);
    return 0;
}
void nx_platform(ElfPlatform *p) {
    memset(p, 0, sizeof *p);
    p->alloc = nx_alloc; p->reserve = nx_reserve; p->map = nx_map; p->protect = nx_protect;
}
