#ifndef CK_NET_H
#define CK_NET_H

#include <ck/types.h>

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_IPPROTO_TCP    6u
#define NET_IPV4_VERSION   4u
#define NET_ETH_HEADER_SIZE       14u
#define NET_IPV4_MIN_HEADER_SIZE  20u
#define NET_TCP_MIN_HEADER_SIZE   20u

/* Byte offsets are used for alignment-safe parsing from untrusted packet buffers. */
#define NET_ETH_OFFSET_ETHERTYPE_HI 12u
#define NET_ETH_OFFSET_ETHERTYPE_LO 13u
#define NET_IPV4_OFFSET_VERSION_IHL 0u
#define NET_IPV4_OFFSET_PROTOCOL    9u

#define NET_READ_BE16(buf, hi_off, lo_off) \
    (u16)((((u16)(buf)[(hi_off)]) << 8) | ((u16)(buf)[(lo_off)]))

struct net_eth_header {
    u8 dst_mac[6];
    u8 src_mac[6];
    u16 ethertype_be;
} __attribute__((packed));

/* Protocol structs are kept for future structured parsing/API expansion. */
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
