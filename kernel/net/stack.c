#include <ck/kernel.h>
#include <ck/net.h>

struct net_boot_summary {
    int stack_ready;
    int has_ipv4;
    int has_tcp;
    u16 ethertype;
};

static struct net_boot_summary g_boot_net;

void net_init(void)
{
    const u8 *packet = ck_boot_network_packet_data();
    u32 packet_size = ck_boot_network_packet_size();
    u32 min_tcp_frame_size = NET_ETH_HEADER_SIZE + NET_IPV4_MIN_HEADER_SIZE + NET_TCP_MIN_HEADER_SIZE;

    g_boot_net.stack_ready = 1;
    g_boot_net.has_ipv4 = 0;
    g_boot_net.has_tcp = 0;
    g_boot_net.ethertype = 0;

    if (!packet) {
        ck_puts("[net] stack: no boot network packet pointer available\n");
        return;
    }

    if (packet_size < NET_ETH_HEADER_SIZE) {
        ck_puts("[net] stack: no boot ethernet frame available\n");
        return;
    }

    u16 ethertype = NET_READ_BE16(packet, NET_ETH_OFFSET_ETHERTYPE_HI, NET_ETH_OFFSET_ETHERTYPE_LO);
    g_boot_net.ethertype = ethertype;

    if (ethertype != NET_ETHERTYPE_IPV4) {
        ck_printk("[net] stack: unsupported boot ethertype 0x%04x\n", (unsigned int)ethertype);
        return;
    }

    g_boot_net.has_ipv4 = 1;

    u32 ipv4_packet_size = packet_size - NET_ETH_HEADER_SIZE;
    if (ipv4_packet_size < NET_IPV4_MIN_HEADER_SIZE) {
        ck_printk("[net] stack: ipv4 header too small (%u bytes)\n", (unsigned int)ipv4_packet_size);
        return;
    }

    const u8 *ipv4 = packet + NET_ETH_HEADER_SIZE;
    u8 ipv4_version = (u8)(ipv4[NET_IPV4_OFFSET_VERSION_IHL] >> 4);
    u8 ipv4_protocol = ipv4[NET_IPV4_OFFSET_PROTOCOL];

    if (ipv4_version != NET_IPV4_VERSION) {
        ck_printk("[net] stack: unexpected ipv4 version %u\n",
                  (unsigned int)ipv4_version);
        return;
    }

    if (ipv4_protocol == NET_IPPROTO_TCP) {
        if (packet_size >= min_tcp_frame_size) {
            g_boot_net.has_tcp = 1;
        } else {
            ck_printk("[net] stack: tcp header too small (%u bytes total frame)\n",
                      (unsigned int)packet_size);
        }
    }
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
