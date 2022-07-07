#include <platform_config.h>
#include <mm/core_memprot.h>
#include <kernel/linker.h>
#include <initcall.h>

#include <assert.h>

#include <bstgw/dma_pool.h>

register_phys_mem(MEM_AREA_IO_SEC, TEE_ENET_DMA_START, TEE_ENET_DMA_SIZE);

tee_mm_pool_t tee_mm_sec_enet_dma __used;

static TEE_Result tee_enet_dma_pool_init(void)
{
	if(!tee_mm_init(&tee_mm_sec_enet_dma, TEE_ENET_DMA_START, TEE_ENET_DMA_START + TEE_ENET_DMA_SIZE,
		SMALL_PAGE_SHIFT, TEE_MM_POOL_NO_FLAGS)) {
		EMSG("Failed ENET DMA pool initialization");
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	return TEE_SUCCESS;
}

vaddr_t bstgw_dma_alloc_coherent(size_t len, paddr_t *out_pa) {
	assert(out_pa);

	tee_mm_entry_t *chunk = tee_mm_alloc(&tee_mm_sec_enet_dma, len);
	if (!chunk) return (vaddr_t)0; 

	uintptr_t pa = tee_mm_get_smem(chunk);
	vaddr_t va = (vaddr_t)phys_to_virt(pa, MEM_AREA_IO_SEC);

	if (!va) {
		EMSG("Failed to get VA for allocated DMA chunk");
		tee_mm_free(chunk);
		return (vaddr_t)0;
	}

	*out_pa = pa;
	return va;
}

void bstgw_dma_free_coherent(size_t len, vaddr_t va, paddr_t pa) {
	tee_mm_entry_t *chk_ref = tee_mm_find(&tee_mm_sec_enet_dma, pa);
	if(chk_ref) {
		// note: partial free'ing is not supported here
		assert(chk_ref->size == len);
		// TODO: could do some checks here
		//assert(virt_to_phys(tee_mm_get_smem(chk_ref)) == va);
		tee_mm_free(chk_ref);
	}
}


// we need it ready before dt_probe() || TODO: are the tee_mm dependencies ready?
service_init_late(tee_enet_dma_pool_init);
