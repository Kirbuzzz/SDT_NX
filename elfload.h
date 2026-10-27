/* elfload: cargador minimo de .so ARM64 (Android) pensado para libnx.
 * Portable: no depende de <elf.h>. Toda la parte dependiente de plataforma
 * (memoria, permisos, resolucion de simbolos) va por ElfPlatform. */
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
    void *(*alloc)(size_t size, size_t align);               /* memoria RW, puesta a cero */
    int   (*protect)(void *addr, size_t len, int prot);      /* en Switch: JIT/code memory */
    void *(*resolve)(const char *sym, void *user);           /* NULL si no existe */
    void  (*unresolved)(const char *mod, const char *sym, int weak, void *user);
    void  *user;
    void  *stub;                                             /* destino de simbolos no resueltos */
} ElfPlatform;

typedef struct ElfModule {
    char name[64];
    uint8_t *base; size_t size;
    const Elf64Sym *symtab; const char *strtab; uint32_t nsyms;
    void (*init)(void); void (**init_array)(void); size_t init_count;
    size_t n_reloc, n_unres;
    Elf64Phdr ph[16]; int nph;
} ElfModule;

int   elf_load(ElfModule *m, const char *name, const uint8_t *file, size_t flen, const ElfPlatform *p);
int   elf_protect(ElfModule *m, const ElfPlatform *p);   /* llamar tras relocar */
void *elf_sym(const ElfModule *m, const char *name);     /* simbolo exportado o NULL */
#endif
