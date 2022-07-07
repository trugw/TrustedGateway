#ifndef BSTGW_FEC_NW_MMIO_TRAPS_H
#define BSTGW_FEC_NW_MMIO_TRAPS_H

#include <mm/core_mmu.h>
#include <secloak/emulation.h>
#include <stdlib.h>

bool fec_emu_check(struct region *region, paddr_t address,
	enum emu_state state, uint32_t *value, int size, bool sign);

#endif /* BSTGW_FEC_NW_MMIO_TRAPS_H */