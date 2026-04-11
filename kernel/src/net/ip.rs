//! IPv4 layer (Layer 3).

/// An IPv4 address.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ipv4Addr(pub [u8; 4]);

impl Ipv4Addr {
    pub const LOOPBACK: Ipv4Addr = Ipv4Addr([127, 0, 0, 1]);
    pub const BROADCAST: Ipv4Addr = Ipv4Addr([255, 255, 255, 255]);
    pub const ANY: Ipv4Addr = Ipv4Addr([0, 0, 0, 0]);
}

/// IPv4 packet (simplified — no options support).
#[derive(Debug, Clone)]
pub struct IpPacket {
    pub src: Ipv4Addr,
    pub dst: Ipv4Addr,
    pub protocol: u8,
    pub ttl: u8,
    pub payload: alloc::vec::Vec<u8>,
}

impl IpPacket {
    pub const PROTO_ICMP: u8 = 1;
    pub const PROTO_TCP: u8 = 6;
    pub const PROTO_UDP: u8 = 17;

    /// Parse an IPv4 packet from a byte slice.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if data.len() < 20 {
            return None;
        }
        let version_ihl = data[0];
        if (version_ihl >> 4) != 4 {
            return None; // not IPv4
        }
        let ihl = ((version_ihl & 0x0F) * 4) as usize;
        let ttl = data[8];
        let protocol = data[9];
        let src = Ipv4Addr(data[12..16].try_into().ok()?);
        let dst = Ipv4Addr(data[16..20].try_into().ok()?);
        let payload = data[ihl..].to_vec();
        Some(IpPacket { src, dst, protocol, ttl, payload })
    }

    /// Calculate IPv4 header checksum.
    pub fn checksum(header: &[u8]) -> u16 {
        let mut sum: u32 = 0;
        for chunk in header.chunks(2) {
            let word = if chunk.len() == 2 {
                u16::from_be_bytes([chunk[0], chunk[1]]) as u32
            } else {
                (chunk[0] as u32) << 8
            };
            sum += word;
        }
        while sum >> 16 != 0 {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        !(sum as u16)
    }
}
