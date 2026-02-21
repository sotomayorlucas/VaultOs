#ifndef VAULTOS_VMM_H
#define VAULTOS_VMM_H

#include "../lib/types.h"
#include "../../include/vaultos/boot_info.h"

void     vmm_init(BootInfo *boot_info);
uint64_t vmm_get_kernel_pml4(void);
uint64_t vmm_create_user_space(void);
void     vmm_destroy_user_space(uint64_t pml4_phys);
void     vmm_switch_address_space(uint64_t pml4_phys);

#endif /* VAULTOS_VMM_H */
