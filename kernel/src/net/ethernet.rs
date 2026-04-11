//! Ethernet layer (Layer 2).

/// An Ethernet MAC address.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MacAddress(pub [u8; 6]);

impl MacAddress {
    pub const BROADCAST: MacAddress = MacAddress([0xFF; 6]);
    pub const ZERO: MacAddress = MacAddress([0x00; 6]);
}

/// Ethernet frame.
#[derive(Debug, Clone)]
pub struct EthernetFrame {
    pub dst: MacAddress,
    pub src: MacAddress,
    pub ethertype: u16,
    pub payload: alloc::vec::Vec<u8>,
}

impl EthernetFrame {
    pub const ETHERTYPE_IPV4: u16 = 0x0800;
    pub const ETHERTYPE_ARP: u16 = 0x0806;
    pub const ETHERTYPE_IPV6: u16 = 0x86DD;

    /// Parse an Ethernet frame from a byte slice.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if data.len() < 14 {
            return None;
        }
        let dst = MacAddress(data[0..6].try_into().ok()?);
        let src = MacAddress(data[6..12].try_into().ok()?);
        let ethertype = u16::from_be_bytes([data[12], data[13]]);
        let payload = data[14..].to_vec();
        Some(EthernetFrame { dst, src, ethertype, payload })
    }

    /// Serialize this frame into a byte vector.
    pub fn serialize(&self) -> alloc::vec::Vec<u8> {
        let mut out = alloc::vec::Vec::with_capacity(14 + self.payload.len());
        out.extend_from_slice(&self.dst.0);
        out.extend_from_slice(&self.src.0);
        out.extend_from_slice(&self.ethertype.to_be_bytes());
        out.extend_from_slice(&self.payload);
        out
    }
}
