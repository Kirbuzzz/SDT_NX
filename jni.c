/* JNI falso: una JavaVM y un JNIEnv minimos. Todo lo no implementado cae en un stub
 * que escribe el NOMBRE de la funcion JNI en el log y detiene el programa. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdt.h"

typedef int32_t jint;
typedef struct { uint32_t magic; char name[100]; } JCls;
typedef struct { uint32_t magic; char *utf; } JStr;
typedef struct { uint32_t magic; JCls *cls; char name[64]; char sig[96]; } JMeth;
typedef struct { const char *name; const char *sig; void *fn; } JNativeMethod;

static void jni_stub_hit(int i);
#include "jni_names.inc"
#include "jni_stubs.inc"

static void *g_fn[JNI_N];
static const void *g_env_obj;
static void *g_vmfn[8];
static const void *g_vm_obj;

static JCls g_cls[64]; static int g_ncls;
static struct { char cls[100]; char name[64]; char sig[96]; void *fn; JCls *c; } g_nat[1024]; static int g_nnat;

static void jni_stub_hit(int i) {
    LOG("!! JNI sin implementar: %s (indice %d)\n", jni_names[i], i);
    fatal_wait("funcion JNI sin implementar");
}

static JCls *cls_get(const char *n) {
    for (int i = 0; i < g_ncls; i++) if (!strcmp(g_cls[i].name, n)) return &g_cls[i];
    if (g_ncls >= 64) return &g_cls[0];
    JCls *c = &g_cls[g_ncls++]; c->magic = 0x434c5331; snprintf(c->name, sizeof c->name, "%s", n);
    return c;
}

/* ---- JavaVM ---- */
static jint vm_destroy(void *vm) { (void)vm; return 0; }
static jint vm_attach(void *vm, void **penv, void *args) { (void)vm; (void)args; *penv = (void *)&g_env_obj; LOG("JNI AttachCurrentThread\n"); return 0; }
static jint vm_detach(void *vm) { (void)vm; return 0; }
static jint vm_getenv(void *vm, void **penv, jint ver) { (void)vm; LOG("JNI GetEnv(version 0x%x)\n", ver); *penv = (void *)&g_env_obj; return 0; }

/* ---- JNIEnv (solo lo imprescindible por ahora) ---- */
static jint   j_GetVersion(void *e) { (void)e; return 0x10006; }
static void  *j_FindClass(void *e, const char *n) { (void)e; LOG("JNI FindClass(\"%s\")\n", n); return cls_get(n); }
static void  *j_GetObjectClass(void *e, void *o) { (void)e; (void)o; return cls_get("java/lang/Object"); }
static void  *j_NewRef(void *e, void *o) { (void)e; return o; }
static void   j_DelRef(void *e, void *o) { (void)e; (void)o; }
static uint8_t j_IsSame(void *e, void *a, void *b) { (void)e; return a == b; }
static jint   j_Zero(void) { return 0; }
static void  *j_Null(void) { return NULL; }
static void   j_Void(void) {}
static void  *j_PopLocalFrame(void *e, void *o) { (void)e; return o; }
static uint8_t j_ExceptionCheck(void *e) { (void)e; return 0; }
static void  *j_GetMethodID(void *e, JCls *c, const char *n, const char *s) {
    (void)e; LOG("JNI GetMethodID(%s, %s, %s)\n", c->name, n, s);
    JMeth *m = calloc(1, sizeof *m); m->magic = 0x4d455448; m->cls = c;
    snprintf(m->name, sizeof m->name, "%s", n); snprintf(m->sig, sizeof m->sig, "%s", s); return m;
}
static void  *j_GetStaticMethodID(void *e, JCls *c, const char *n, const char *s) {
    LOG("JNI GetStaticMethodID(%s, %s, %s)\n", c->name, n, s); return j_GetMethodID(e, c, n, s);
}
static void  *j_GetFieldID(void *e, JCls *c, const char *n, const char *s) {
    (void)e; LOG("JNI GetFieldID/GetStaticFieldID(%s, %s, %s)\n", c->name, n, s);
    JMeth *m = calloc(1, sizeof *m); m->magic = 0x4649454c; m->cls = c;
    snprintf(m->name, sizeof m->name, "%s", n); snprintf(m->sig, sizeof m->sig, "%s", s); return m;
}
void *jni_new_string(const char *utf) { JStr *s = calloc(1, sizeof *s); s->magic = 0x53545231; s->utf = strdup(utf); return s; }
static void  *j_NewStringUTF(void *e, const char *u) { (void)e; return jni_new_string(u); }
static jint   j_GetStringUTFLength(void *e, JStr *s) { (void)e; return (jint)strlen(s->utf); }
static const char *j_GetStringUTFChars(void *e, JStr *s, uint8_t *copy) { (void)e; if (copy) *copy = 0; return s->utf; }
static void   j_ReleaseStringUTFChars(void *e, JStr *s, const char *c) { (void)e; (void)s; (void)c; }
static jint   j_GetJavaVM(void *e, void **vm) { (void)e; *vm = (void *)&g_vm_obj; return 0; }
static jint   j_RegisterNatives(void *e, JCls *c, const JNativeMethod *m, jint n) {
    (void)e; LOG("JNI RegisterNatives(%s, %d metodos)\n", c->name, n);
    for (int i = 0; i < n; i++) {
        LOG("   nativo %s %s -> %p\n", m[i].name, m[i].sig, m[i].fn);
        if (g_nnat < 1024) {
            snprintf(g_nat[g_nnat].cls, sizeof g_nat[0].cls, "%s", c->name);
            snprintf(g_nat[g_nnat].name, sizeof g_nat[0].name, "%s", m[i].name);
            snprintf(g_nat[g_nnat].sig, sizeof g_nat[0].sig, "%s", m[i].sig);
            g_nat[g_nnat].fn = m[i].fn; g_nat[g_nnat].c = c; g_nnat++;
        }
    }
    return 0;
}

int jni_find_native(const char *cls, const char *name, void **fn, void **clsobj) {
    for (int i = 0; i < g_nnat; i++)
        if (!strcmp(g_nat[i].cls, cls) && !strcmp(g_nat[i].name, name)) { *fn = g_nat[i].fn; *clsobj = g_nat[i].c; return 0; }
    return -1;
}

void jni_init(void) {
    for (int i = 0; i < JNI_N; i++) g_fn[i] = jni_stub_tab[i];
    g_fn[0] = g_fn[1] = g_fn[2] = g_fn[3] = NULL;
    g_fn[4] = (void *)j_GetVersion;
    g_fn[6] = (void *)j_FindClass;
    g_fn[15] = (void *)j_Null;                 /* ExceptionOccurred */
    g_fn[16] = (void *)j_Void;                 /* ExceptionDescribe */
    g_fn[17] = (void *)j_Void;                 /* ExceptionClear    */
    g_fn[19] = (void *)j_Zero;                 /* PushLocalFrame    */
    g_fn[20] = (void *)j_PopLocalFrame;
    g_fn[21] = (void *)j_NewRef;               /* NewGlobalRef      */
    g_fn[22] = (void *)j_DelRef;               /* DeleteGlobalRef   */
    g_fn[23] = (void *)j_DelRef;               /* DeleteLocalRef    */
    g_fn[24] = (void *)j_IsSame;
    g_fn[25] = (void *)j_NewRef;               /* NewLocalRef       */
    g_fn[26] = (void *)j_Zero;                 /* EnsureLocalCapacity */
    g_fn[31] = (void *)j_GetObjectClass;
    g_fn[33] = (void *)j_GetMethodID;
    g_fn[94] = (void *)j_GetFieldID;
    g_fn[113] = (void *)j_GetStaticMethodID;
    g_fn[144] = (void *)j_GetFieldID;          /* GetStaticFieldID  */
    g_fn[167] = (void *)j_NewStringUTF;
    g_fn[168] = (void *)j_GetStringUTFLength;
    g_fn[169] = (void *)j_GetStringUTFChars;
    g_fn[170] = (void *)j_ReleaseStringUTFChars;
    g_fn[215] = (void *)j_RegisterNatives;
    g_fn[216] = (void *)j_Zero;                /* UnregisterNatives */
    g_fn[217] = (void *)j_Zero;                /* MonitorEnter      */
    g_fn[218] = (void *)j_Zero;                /* MonitorExit       */
    g_fn[219] = (void *)j_GetJavaVM;
    g_fn[227] = (void *)j_DelRef;              /* DeleteWeakGlobalRef */
    g_fn[228] = (void *)j_ExceptionCheck;
    g_env_obj = g_fn;
    g_vmfn[0] = g_vmfn[1] = g_vmfn[2] = NULL;
    g_vmfn[3] = (void *)vm_destroy; g_vmfn[4] = (void *)vm_attach; g_vmfn[5] = (void *)vm_detach;
    g_vmfn[6] = (void *)vm_getenv;  g_vmfn[7] = (void *)vm_attach;
    g_vm_obj = g_vmfn;
}
void *jni_vm(void)  { return (void *)&g_vm_obj; }
void *jni_env(void) { return (void *)&g_env_obj; }
