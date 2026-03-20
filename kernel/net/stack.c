#include <ck/kernel.h>
#include <ck/net.h>

struct net_boot_summary {
    int stack_ready;
    int has_ipv4;
    int has_tcp;
    u16 ethertype;
};

static struct net_boot_summary g_boot_net;

static u16 be16_to_cpu(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

void net_init(void)
{
    const u8 *packet = ck_boot_network_packet_data();
    u32 packet_size = ck_boot_network_packet_size();

    g_boot_net.stack_ready = 1;
    g_boot_net.has_ipv4 = 0;
    g_boot_net.has_tcp = 0;
    g_boot_net.ethertype = 0;

    if (!packet || packet_size < sizeof(struct net_eth_header)) {
        ck_puts("[net] stack: no boot ethernet frame available\n");
        return;
    }

    const struct net_eth_header *eth = (const struct net_eth_header *)packet;
    u16 ethertype = be16_to_cpu(eth->ethertype_be);
    g_boot_net.ethertype = ethertype;

    if (ethertype != NET_ETHERTYPE_IPV4) {
        ck_printk("[net] stack: unsupported boot ethertype 0x%04x\n", (unsigned int)ethertype);
        return;
    }

    g_boot_net.has_ipv4 = 1;

    u32 l3_size = packet_size - (u32)sizeof(struct net_eth_header);
    if (l3_size < sizeof(struct net_ipv4_header))
        return;

    const struct net_ipv4_header *ipv4 =
        (const struct net_ipv4_header *)(packet + sizeof(struct net_eth_header));

    if ((ipv4->version_ihl >> 4) != 4)
        return;

    if (ipv4->protocol == NET_IPPROTO_TCP)
        g_boot_net.has_tcp = 1;
}

int net_stack_ready(void)
{
    return g_boot_net.stack_ready;
}

int net_has_boot_ipv4(void)
{
    return g_boot_net.has_ipv4;
}

int net_has_boot_tcp(void)
{
    return g_boot_net.has_tcp;
}

u16 net_boot_ethertype(void)
{
    return g_boot_net.ethertype;
}
