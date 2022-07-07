#include <platform_config.h>
#include <mm/core_memprot.h>
#include <kernel/linker.h>
#include <initcall.h>

#include <bstgw/jit_pool.h>

register_phys_mem(MEM_AREA_TEE_RAM, TEE_JIT_START, TEE_JIT_SIZE);

tee_mm_pool_t tee_mm_sec_jit __used;

static TEE_Result tee_jit_pool_init(void)
{
	if(!tee_mm_init(&tee_mm_sec_jit, TEE_JIT_START, TEE_JIT_START + TEE_JIT_SIZE,
		SMALL_PAGE_SHIFT, TEE_MM_POOL_NO_FLAGS)) {
		EMSG("Failed JIT pool initialization");
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	return TEE_SUCCESS;
}

driver_init_late(tee_jit_pool_init);
//service_init_late(tee_jit_pool_init);
