#ifndef CK_NET_H
#define CK_NET_H

#include <ck/types.h>

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_IPPROTO_TCP    6u

struct net_eth_header {
    u8 dst_mac[6];
    u8 src_mac[6];
    u16 ethertype_be;
} __attribute__((packed));

struct net_ipv4_header {
    u8 version_ihl;
    u8 dscp_ecn;
    u16 total_length_be;
    u16 identification_be;
    u16 flags_fragment_be;
    u8 ttl;
    u8 protocol;
    u16 header_checksum_be;
    u32 src_addr_be;
    u32 dst_addr_be;
} __attribute__((packed));

void net_init(void);
int net_stack_ready(void);
int net_has_boot_ipv4(void);
int net_has_boot_tcp(void);
u16 net_boot_ethertype(void);

#endif /* CK_NET_H */
