#include "elfload.h"
#include <string.h>
#include <stdio.h>

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_TLS 7
enum { DT_NULL=0, DT_PLTRELSZ=2, DT_HASH=4, DT_STRTAB=5, DT_SYMTAB=6, DT_RELA=7, DT_RELASZ=8,
       DT_INIT=12, DT_JMPREL=23, DT_INIT_ARRAY=25, DT_INIT_ARRAYSZ=27, DT_RELRSZ=35, DT_RELR=36 };
#define DT_GNU_HASH 0x6ffffef5
#define DT_ANDROID_REL  0x6000000f
#define DT_ANDROID_RELA 0x60000011
#define R_RELATIVE 1027
#define R_ABS64    257
#define R_GLOB_DAT 1025
#define R_JUMP_SLOT 1026
#define R_IRELATIVE 1032

static size_t up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static uint32_t gnu_nsyms(const uint32_t *h) {
    uint32_t nb = h[0], symoff = h[1], bloom = h[2];
    const uint32_t *buckets = h + 4 + bloom * 2, *chains = buckets + nb;
    uint32_t max = 0;
    for (uint32_t i = 0; i < nb; i++) if (buckets[i] > max) max = buckets[i];
    if (max < symoff) return symoff;
    while (!(chains[max - symoff] & 1)) max++;
    return max + 1;
}

int elf_load(ElfModule *m, const char *name, const uint8_t *f, size_t flen, const ElfPlatform *pl) {
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name);
    if (flen < sizeof(Elf64Ehdr)) return -1;
    const Elf64Ehdr *eh = (const Elf64Ehdr *)f;
    if (memcmp(eh->e_ident, "\x7f""ELF", 4) || eh->e_ident[4] != 2 || eh->e_machine != 183) return -2; /* AArch64 */
    if (eh->e_phnum > 16 || eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64Phdr) > flen) return -3;
    memcpy(m->ph, f + eh->e_phoff, eh->e_phnum * sizeof(Elf64Phdr)); m->nph = eh->e_phnum;

    uint64_t maxv = 0, align = 0x1000; const Elf64Phdr *dynp = NULL;
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type == PT_TLS) return -10;               /* no soportado (no aparece en estas libs) */
        if (p->p_type == PT_DYNAMIC) dynp = p;
        if (p->p_type != PT_LOAD) continue;
        if (p->p_offset + p->p_filesz > flen) return -4;
        if (p->p_vaddr + p->p_memsz > maxv) maxv = p->p_vaddr + p->p_memsz;
        if (p->p_align > align) align = p->p_align;
    }
    if (!dynp) return -5;
    m->size = up(maxv, 0x4000);
    m->base = pl->alloc(m->size, align);
    if (!m->base) return -6;
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD) memcpy(m->base + p->p_vaddr, f + p->p_offset, p->p_filesz);
    }

    /* .dynamic */
    const Elf64Dyn *d = (const Elf64Dyn *)(m->base + dynp->p_vaddr);
    const Elf64Rela *rela = NULL, *jmp = NULL; size_t relasz = 0, jmpsz = 0, initsz = 0;
    const uint32_t *gh = NULL, *sh = NULL; uint64_t init = 0, initarr = 0;
    for (; d->d_tag != DT_NULL; d++) switch (d->d_tag) {
        case DT_STRTAB: m->strtab = (const char *)(m->base + d->d_val); break;
        case DT_SYMTAB: m->symtab = (const Elf64Sym *)(m->base + d->d_val); break;
        case DT_RELA: rela = (const Elf64Rela *)(m->base + d->d_val); break;
        case DT_RELASZ: relasz = d->d_val; break;
        case DT_JMPREL: jmp = (const Elf64Rela *)(m->base + d->d_val); break;
        case DT_PLTRELSZ: jmpsz = d->d_val; break;
        case DT_GNU_HASH: gh = (const uint32_t *)(m->base + d->d_val); break;
        case DT_HASH: sh = (const uint32_t *)(m->base + d->d_val); break;
        case DT_INIT: init = d->d_val; break;
        case DT_INIT_ARRAY: initarr = d->d_val; break;
        case DT_INIT_ARRAYSZ: initsz = d->d_val; break;
        case DT_RELR: case DT_RELRSZ: case DT_ANDROID_REL: case DT_ANDROID_RELA: return -11; /* relocs empaquetadas */
    }
    if (!m->symtab || !m->strtab) return -7;
    m->nsyms = gh ? gnu_nsyms(gh) : (sh ? sh[1] : 0);
    if (init) m->init = (void (*)(void))(m->base + init);
    if (initarr) { m->init_array = (void (**)(void))(m->base + initarr); m->init_count = initsz / 8; }

    /* relocaciones */
    for (int pass = 0; pass < 2; pass++) {
        const Elf64Rela *r = pass ? jmp : rela; size_t n = (pass ? jmpsz : relasz) / sizeof *r;
        for (size_t i = 0; i < n; i++, r++) {
            uint32_t type = (uint32_t)r->r_info, si = (uint32_t)(r->r_info >> 32);
            if (r->r_offset + 8 > m->size) return -8;
            uint64_t *where = (uint64_t *)(m->base + r->r_offset);
            uint64_t S = 0;
            if (type == R_RELATIVE) { *where = (uint64_t)(uintptr_t)m->base + r->r_addend; m->n_reloc++; continue; }
            if (type == R_ABS64 || type == R_GLOB_DAT || type == R_JUMP_SLOT) {
                if (si) {
                    const Elf64Sym *s = &m->symtab[si];
                    const char *nm = m->strtab + s->st_name;
                    if (s->st_shndx != 0) S = (uint64_t)(uintptr_t)m->base + s->st_value;   /* definido aqui */
                    else {
                        void *a = pl->resolve ? pl->resolve(nm, pl->user) : NULL;
                        if (!a) { int weak = (s->st_info >> 4) == 2;
                                  if (pl->unresolved) pl->unresolved(m->name, nm, weak, pl->user);
                                  m->n_unres++; a = weak ? NULL : pl->stub; }
                        S = (uint64_t)(uintptr_t)a;
                    }
                }
                *where = S + (type == R_JUMP_SLOT ? 0 : r->r_addend); m->n_reloc++; continue;
            }
            if (type == R_IRELATIVE) return -12;
            return -9;  /* tipo desconocido */
        }
    }
    return 0;
}

int elf_protect(ElfModule *m, const ElfPlatform *pl) {
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type != PT_LOAD) continue;
        int prot = ((p->p_flags & 4) ? ELF_PROT_R : 0) | ((p->p_flags & 2) ? ELF_PROT_W : 0) | ((p->p_flags & 1) ? ELF_PROT_X : 0);
        size_t s = p->p_vaddr & ~0xfffUL, e = up(p->p_vaddr + p->p_memsz, 0x1000);
        if (pl->protect(m->base + s, e - s, prot)) return -1;
    }
    return 0;
}

void *elf_sym(const ElfModule *m, const char *name) {
    for (uint32_t i = 1; i < m->nsyms; i++) {
        const Elf64Sym *s = &m->symtab[i];
        if (s->st_shndx && (s->st_info & 0xf) != 0 && !strcmp(m->strtab + s->st_name, name))
            return m->base + s->st_value;
    }
    return NULL;
}
