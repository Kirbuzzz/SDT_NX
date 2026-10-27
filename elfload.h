/* elfload v2: cargador de .so ARM64 (Android) para libnx.
 * Se prepara la imagen en memoria normal ("staging"), se relocan los punteros
 * con la direccion FINAL, y luego elf_commit() la mapea y fija permisos. */
#ifndef ELFLOAD_H
#define ELFLOAD_H
#include <stddef.h>
#include <stdint.h>

typedef struct { unsigned char e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf64Ehdr;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } Elf64Phdr;
typedef struct { int64_t d_tag; uint64_t d_val; } Elf64Dyn;
typedef struct { uint32_t st_name; uint8_t st_info, st_other; uint16_t st_shndx; uint64_t st_value, st_size; } Elf64Sym;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } Elf64Rela;

#define ELF_PROT_R 1
#define ELF_PROT_W 2
#define ELF_PROT_X 4

typedef struct ElfPlatform {
    void *(*alloc)(size_t size, size_t align);               /* staging RW, a cero            */
    void *(*reserve)(size_t size, size_t align);             /* direccion final; NULL = usar staging */
    int   (*map)(void *dst, void *src, size_t size);         /* staging -> dst (src queda inaccesible) */
    int   (*protect)(void *addr, size_t len, int prot);      /* ELF_PROT_*                    */
    void *(*resolve)(const char *sym, void *user);           /* NULL si no existe             */
    void *(*make_stub)(const char *sym, void *user);         /* stub para simbolos no resueltos (no weak) */
    void  (*unresolved)(const char *mod, const char *sym, int weak, void *user);
    void  *user;
} ElfPlatform;

typedef struct ElfModule {
    char name[64];
    uint8_t *base;            /* staging (invalido tras elf_commit con reserve) */
    uint8_t *load;            /* direccion final de ejecucion                   */
    size_t size;
    const Elf64Sym *symtab; const char *strtab; uint32_t nsyms;   /* basados en 'load' */
    void (*init)(void); void (**init_array)(void); size_t init_count;
    size_t n_reloc, n_unres;
    Elf64Phdr ph[16]; int nph;
} ElfModule;

int   elf_load(ElfModule *m, const char *name, const uint8_t *file, size_t flen, const ElfPlatform *p);
int   elf_commit(ElfModule *m, const ElfPlatform *p);   /* map + permisos por segmento */
void  elf_run_init(ElfModule *m);                       /* DT_INIT + DT_INIT_ARRAY     */
void *elf_sym(const ElfModule *m, const char *name);    /* exportado (solo tras commit) */
#endif
