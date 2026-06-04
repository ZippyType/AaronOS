#include "elf.h"
#include "fat16.h"
#include <stddef.h>

extern void print(const char* str);
extern void print_hex(uint32_t val);
extern void* malloc(size_t size);
extern void free(void* ptr);

int elf_load(const char* filename, uint32_t* entry) {
    char* buf = malloc(4096);
    if (!buf) { print("elf: malloc failed\n"); return -1; }
    int len = fat16_read_file((char*)filename, buf, 4096);
    if (len <= 0) { print("elf: file not found\n"); free(buf); return -1; }

    elf32_hdr_t* hdr = (elf32_hdr_t*)buf;
    if (*(uint32_t*)hdr->e_ident != ELF_MAGIC) {
        print("elf: bad magic\n");
        free(buf);
        return -1;
    }
    if (hdr->e_ident[4] != 1) { print("elf: not 32-bit\n"); free(buf); return -1; }
    if (hdr->e_ident[5] != 1) { print("elf: not little-endian\n"); free(buf); return -1; }
    if (hdr->e_type != 2) { print("elf: not executable\n"); free(buf); return -1; }

    if (entry) *entry = hdr->e_entry;

    elf32_phdr_t* phdr = (elf32_phdr_t*)(buf + hdr->e_phoff);
    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint32_t vaddr = phdr[i].p_vaddr;
            uint32_t filesz = phdr[i].p_filesz;
            uint32_t memsz = phdr[i].p_memsz;
            uint8_t* dest = (uint8_t*)vaddr;
            uint8_t* src = (uint8_t*)buf + phdr[i].p_offset;
            for (uint32_t j = 0; j < filesz; j++) dest[j] = src[j];
            for (uint32_t j = filesz; j < memsz; j++) dest[j] = 0;
        }
    }
    free(buf);
    return 0;
}
