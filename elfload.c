#include "elfload.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    if (memcmp(eh->e_ident, "\x7f""ELF", 4) || eh->e_ident[4] != 2 || eh->e_machine != 183) return -2;
    if (eh->e_phnum > 16 || eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64Phdr) > flen) return -3;
    memcpy(m->ph, f + eh->e_phoff, eh->e_phnum * sizeof(Elf64Phdr)); m->nph = eh->e_phnum;

    uint64_t maxv = 0, align = 0x1000; const Elf64Phdr *dynp = NULL;
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type == PT_TLS) return -10;
        if (p->p_type == PT_DYNAMIC) dynp = &m->ph[i];
        if (p->p_type != PT_LOAD) continue;
        if (p->p_offset + p->p_filesz > flen) return -4;
        if (p->p_vaddr + p->p_memsz > maxv) maxv = p->p_vaddr + p->p_memsz;
        if (p->p_align > align) align = p->p_align;
    }
    if (!dynp) return -5;
    m->size = up(maxv, 0x4000);
    uint8_t *stg = pl->alloc(m->size, align);
    if (!stg) return -6;
    uint8_t *load = pl->reserve ? pl->reserve(m->size, align) : stg;
    if (!load) return -13;
    m->base = stg; m->load = load;
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD) memcpy(stg + p->p_vaddr, f + p->p_offset, p->p_filesz);
    }

    const Elf64Dyn *d = (const Elf64Dyn *)(stg + dynp->p_vaddr);
    uint64_t o_rela = 0, o_jmp = 0, relasz = 0, jmpsz = 0, initsz = 0, o_gh = 0, o_sh = 0;
    uint64_t o_init = 0, o_initarr = 0, o_sym = 0, o_str = 0;
    for (; d->d_tag != DT_NULL; d++) switch (d->d_tag) {
        case DT_STRTAB: o_str = d->d_val; break;
        case DT_SYMTAB: o_sym = d->d_val; break;
        case DT_RELA: o_rela = d->d_val; break;
        case DT_RELASZ: relasz = d->d_val; break;
        case DT_JMPREL: o_jmp = d->d_val; break;
        case DT_PLTRELSZ: jmpsz = d->d_val; break;
        case DT_GNU_HASH: o_gh = d->d_val; break;
        case DT_HASH: o_sh = d->d_val; break;
        case DT_INIT: o_init = d->d_val; break;
        case DT_INIT_ARRAY: o_initarr = d->d_val; break;
        case DT_INIT_ARRAYSZ: initsz = d->d_val; break;
        case DT_RELR: case DT_RELRSZ: case DT_ANDROID_REL: case DT_ANDROID_RELA: return -11;
    }
    if (!o_sym || !o_str) return -7;
    const Elf64Sym *symtab_s = (const Elf64Sym *)(stg + o_sym);
    const char *strtab_s = (const char *)(stg + o_str);
    m->symtab = (const Elf64Sym *)(load + o_sym);       /* validos tras commit */
    m->strtab = (const char *)(load + o_str);
    m->nsyms = o_gh ? gnu_nsyms((const uint32_t *)(stg + o_gh)) : (o_sh ? ((const uint32_t *)(stg + o_sh))[1] : 0);
    if (o_init) m->init = (void (*)(void))(load + o_init);
    if (o_initarr) { m->init_array = (void (**)(void))(load + o_initarr); m->init_count = initsz / 8; }

    for (int pass = 0; pass < 2; pass++) {
        const Elf64Rela *r = (const Elf64Rela *)(stg + (pass ? o_jmp : o_rela));
        size_t n = (pass ? jmpsz : relasz) / sizeof *r;
        if (!(pass ? o_jmp : o_rela)) n = 0;
        for (size_t i = 0; i < n; i++, r++) {
            uint32_t type = (uint32_t)r->r_info, si = (uint32_t)(r->r_info >> 32);
            if (r->r_offset + 8 > m->size) return -8;
            uint64_t *where = (uint64_t *)(stg + r->r_offset);      /* se escribe en staging */
            uint64_t S = 0;
            if (type == R_RELATIVE) { *where = (uint64_t)(uintptr_t)load + r->r_addend; m->n_reloc++; continue; }
            if (type == R_ABS64 || type == R_GLOB_DAT || type == R_JUMP_SLOT) {
                if (si) {
                    const Elf64Sym *s = &symtab_s[si];
                    const char *nm = strtab_s + s->st_name;
                    if (s->st_shndx != 0) S = (uint64_t)(uintptr_t)load + s->st_value;
                    else {
                        void *a = pl->resolve ? pl->resolve(nm, pl->user) : NULL;
                        if (!a) {
                            int weak = (s->st_info >> 4) == 2;
                            if (pl->unresolved) pl->unresolved(m->name, nm, weak, pl->user);
                            m->n_unres++;
                            a = weak ? NULL : (pl->make_stub ? pl->make_stub(nm, pl->user) : NULL);
                        }
                        S = (uint64_t)(uintptr_t)a;
                    }
                }
                *where = S + (type == R_JUMP_SLOT ? 0 : r->r_addend); m->n_reloc++; continue;
            }
            if (type == R_IRELATIVE) return -12;
            return -9;
        }
    }
    return 0;
}

int elf_commit(ElfModule *m, const ElfPlatform *pl) {
    if (pl->reserve) {
        if (!pl->map || pl->map(m->load, m->base, m->size)) return -1;
        m->base = NULL;                                  /* staging ya no es accesible */
    }
    size_t np = m->size / 0x1000; uint8_t *fl = calloc(np, 1);
    if (!fl) return -2;
    for (int i = 0; i < m->nph; i++) {
        const Elf64Phdr *p = &m->ph[i];
        if (p->p_type != PT_LOAD || !p->p_memsz) continue;
        uint8_t f = ((p->p_flags & 4) ? ELF_PROT_R : 0) | ((p->p_flags & 2) ? ELF_PROT_W : 0) | ((p->p_flags & 1) ? ELF_PROT_X : 0);
        for (size_t pg = p->p_vaddr >> 12; pg <= (p->p_vaddr + p->p_memsz - 1) >> 12 && pg < np; pg++) fl[pg] |= f;
    }
    int rc = 0;
    for (size_t pg = 0; pg < np && !rc; ) {
        uint8_t f = fl[pg]; size_t e = pg + 1;
        while (e < np && fl[e] == f) e++;
        if (f == 7) rc = -3;                              /* W+X en la misma pagina: no permitido */
        else if (f && pl->protect(m->load + pg * 0x1000, (e - pg) * 0x1000, f)) rc = -4;
        pg = e;
    }
    free(fl);
    return rc;
}

void elf_run_init(ElfModule *m) {
    if (m->init) m->init();
    for (size_t i = 0; i < m->init_count; i++) {
        void (*fn)(void) = m->init_array[i];
        if (fn && fn != (void (*)(void))(intptr_t)-1) fn();
    }
}

void *elf_sym(const ElfModule *m, const char *name) {
    for (uint32_t i = 1; i < m->nsyms; i++) {
        const Elf64Sym *s = &m->symtab[i];
        if (s->st_shndx && (s->st_info & 0xf) != 0 && !strcmp(m->strtab + s->st_name, name))
            return m->load + s->st_value;
    }
    return NULL;
}
