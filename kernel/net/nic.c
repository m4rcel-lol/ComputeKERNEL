#include <ck/kernel.h>
#include <ck/nic.h>

static struct nic_device *nic_devices[NIC_MAX_DEVICES];
static u32 nic_count = 0;

void nic_init(void)
{
    for (u32 i = 0; i < NIC_MAX_DEVICES; i++)
        nic_devices[i] = NULL;
    nic_count = 0;
    ck_puts("[nic] framework: ready (0 registered)\n");
}

int nic_register_device(struct nic_device *dev)
{
    if (!dev)
        return -1;
    if (nic_count >= NIC_MAX_DEVICES)
        return -1;

    nic_devices[nic_count] = dev;
    nic_count++;
    return 0;
}

const struct nic_device *nic_get_device(u32 index)
{
    if (index >= nic_count)
        return NULL;
    return nic_devices[index];
}

u32 nic_device_count(void)
{
    return nic_count;
}
