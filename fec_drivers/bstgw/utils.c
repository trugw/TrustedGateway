#include <drivers/bstgw/utils.h>

#include <npf_router.h>
#include <stdlib.h>
#include <mm/core_memprot.h>

#include <drivers/dt.h>

struct ifnet * bstgw_alloc_etherdev(size_t priv_size) {
    struct ifnet *ndev = malloc(sizeof(struct ifnet));
    if(unlikely( !ndev )) return NULL;

    if(unlikely( ifnet_dev_init(ndev) != 0 )) {
        free(ndev);
        return NULL;
    }

    ndev->priv_data = calloc(1,priv_size);
    if(unlikely( !ndev->priv_data )) {
        free(ndev);
        return NULL;
    }

    return ndev;
}

struct resource * bstgw_platform_get_resource(
    struct device *dev, enum resource_type type, unsigned int idx) {
    // TODO: does not seem to be per-device resources
    //       resources[] prob. just mem. regions?
    if(unlikely( type != dev->resource_type )) return NULL;
    if(unlikely( idx >= dev->num_resources )) return NULL;
    return &dev->resources[idx];
}

vaddr_t
bstgw_devm_ioremap_resource(struct resource *res) {
    // TODO: fine?
    //note: idx 0 also used in dt_enable_device()
    if(unlikely( 
        !core_mmu_add_mapping(MEM_AREA_IO_SEC, res->address[0], res->size[0])
    ))
        return (vaddr_t)0;    
    return core_mmu_get_va(res->address[0], MEM_AREA_IO_SEC);
}