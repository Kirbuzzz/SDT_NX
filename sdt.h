#ifndef SDT_H
#define SDT_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "elfload.h"

/* log + utilidades (main.c) */
void LOG(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void canary_fix(void);                 /* restaura TP+0x28 tras cualquier IPC (ver notas) */
void fatal_wait(const char *why);      /* muestra el motivo y espera '+'; no vuelve        */
extern int g_guest;                    /* 1 mientras se ejecuta codigo de las .so          */

/* stubs por simbolo (stubs.c) */
void *stub_make(const char *name);

/* plataforma Switch (nxplat.c) */
void nx_platform(ElfPlatform *p);

/* shims (shims.c / dl.c) */
void *shim_lookup(const char *name);
void *sdt_dlopen(const char *path, int flags);
void *sdt_dlsym(void *h, const char *name);
int   sdt_dlclose(void *h);
char *sdt_dlerror(void);
int   mod_load(const char *dir, const char *name, const ElfPlatform *pl);
ElfModule *mod_find(const char *name);
void *mod_export(const char *sym);

/* JNI falso (jni.c) */
void  jni_init(void);
void *jni_vm(void);
void *jni_env(void);
void *jni_new_string(const char *utf);
int   jni_find_native(const char *cls, const char *name, void **fn, void **clsobj);
#endif
