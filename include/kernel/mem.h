#ifndef __MEM_H__
#define __MEM_H__

#include <stdint.h>
#include <stddef.h>

void *kmalloc_page(size_t size);
void *kcalloc_page(size_t size);
void *kmalloc(size_t size);
void *kcalloc(size_t size);
void kfree(void *addr);
void kfree_page(void *addr);
void mem_stats();
int mem_init(void *addr, size_t size);

#endif /* __MEM_H__ */
