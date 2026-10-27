/* Funciones "del sistema Android" que ofrecemos al codigo cargado.
 * Todo lo que no este aqui (ni en otro modulo cargado) cae en un stub que lo nombra. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sdt.h"

typedef struct { const char *n; void *p; } Shim;

static int  sh_cxa_atexit(void (*f)(void *), void *a, void *d) { (void)f; (void)a; (void)d; return 0; }
static void sh_cxa_finalize(void *d) { (void)d; }
static void sh_stack_chk_fail(void) {
    LOG("!! __stack_chk_fail: canario corrupto. Si ocurre justo tras una llamada a un shim con E/S, es el problema del TLS (ver notas).\n");
    fatal_wait("__stack_chk_fail");
}
static int sh_log_write(int prio, const char *tag, const char *msg) { LOG("[android %d %s] %s\n", prio, tag ? tag : "", msg ? msg : ""); return 0; }
static int sh_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    char buf[512]; vsnprintf(buf, sizeof buf, fmt, ap); return sh_log_write(prio, tag, buf);
}
static int sh_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = sh_log_vprint(prio, tag, fmt, ap); va_end(ap); return r;
}

static const Shim g_shims[] = {
    /* libc directa (newlib) */
    {"malloc", (void *)malloc}, {"free", (void *)free}, {"calloc", (void *)calloc}, {"realloc", (void *)realloc},
    {"memcpy", (void *)memcpy}, {"memmove", (void *)memmove}, {"memset", (void *)memset}, {"memcmp", (void *)memcmp},
    {"strlen", (void *)strlen}, {"strcmp", (void *)strcmp}, {"strncmp", (void *)strncmp},
    {"strcpy", (void *)strcpy}, {"strncpy", (void *)strncpy}, {"strchr", (void *)strchr},
    {"strrchr", (void *)strrchr}, {"strstr", (void *)strstr},
    {"snprintf", (void *)snprintf}, {"vsnprintf", (void *)vsnprintf},
    /* C++/runtime */
    {"__cxa_atexit", (void *)sh_cxa_atexit}, {"__cxa_finalize", (void *)sh_cxa_finalize},
    {"__stack_chk_fail", (void *)sh_stack_chk_fail},
    /* cargador dinamico */
    {"dlopen", (void *)sdt_dlopen}, {"dlsym", (void *)sdt_dlsym}, {"dlclose", (void *)sdt_dlclose}, {"dlerror", (void *)sdt_dlerror},
    /* log de Android */
    {"__android_log_write", (void *)sh_log_write}, {"__android_log_print", (void *)sh_log_print},
    {"__android_log_vprint", (void *)sh_log_vprint},
    {NULL, NULL}
};

void *shim_lookup(const char *name) {
    for (const Shim *s = g_shims; s->n; s++) if (!strcmp(s->n, name)) return s->p;
    return NULL;
}
