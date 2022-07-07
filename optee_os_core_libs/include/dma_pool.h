#include <mm/tee_mm.h>
#include <mm/core_memprot.h>

extern tee_mm_pool_t tee_mm_sec_enet_dma;

vaddr_t bstgw_dma_alloc_coherent(size_t, paddr_t *);
void bstgw_dma_free_coherent(size_t, vaddr_t, paddr_t);