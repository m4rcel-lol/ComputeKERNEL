#ifndef CK_NIC_H
#define CK_NIC_H

#include <ck/types.h>

#define NIC_NAME_MAX_LEN 16u
#define NIC_MAX_DEVICES  4u

struct nic_device {
    char name[NIC_NAME_MAX_LEN];
    u8 mac_addr[6];
    void *driver_data;
};

void nic_init(void);
int nic_register_device(struct nic_device *dev);
const struct nic_device *nic_get_device(u32 index);
u32 nic_device_count(void);

#endif /* CK_NIC_H */
