/* Un stub distinto por cada simbolo no resuelto: si el codigo cargado lo llama,
 * se escribe su NOMBRE en el log y el programa se detiene (asi sabemos que shim falta). */
#include <string.h>
#include <stdlib.h>
#include "sdt.h"

#define NSTUB 1024
static const char *g_stub_names[NSTUB];
static int g_nstub;
static void stub_hit(int i);
#include "stubs.inc"

void *stub_make(const char *name) {
    for (int i = 0; i < g_nstub; i++) if (!strcmp(g_stub_names[i], name)) return (void *)g_stub_fns[i];
    if (g_nstub >= NSTUB) return (void *)g_stub_fns[NSTUB - 1];
    g_stub_names[g_nstub] = strdup(name);
    return (void *)g_stub_fns[g_nstub++];
}
static void stub_hit(int i) {
    LOG("!! FALTA SHIM: se llamo a '%s' (stub %d)\n", i < g_nstub ? g_stub_names[i] : "?", i);
    fatal_wait("simbolo sin implementar");
}
