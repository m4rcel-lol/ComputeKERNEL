//! UDP layer (Layer 4) — stub implementation.

use alloc::vec::Vec;

/// A UDP datagram.
#[derive(Debug, Clone)]
pub struct UdpDatagram {
    pub src_port: u16,
    pub dst_port: u16,
    pub payload: Vec<u8>,
}

impl UdpDatagram {
    /// Parse a UDP datagram from a byte slice.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if data.len() < 8 {
            return None;
        }
        let src_port = u16::from_be_bytes([data[0], data[1]]);
        let dst_port = u16::from_be_bytes([data[2], data[3]]);
        let length = u16::from_be_bytes([data[4], data[5]]) as usize;
        if length < 8 || length > data.len() {
            return None;
        }
        let payload = data[8..length].to_vec();
        Some(UdpDatagram { src_port, dst_port, payload })
    }

    /// Serialize this datagram into a byte vector (no checksum computation).
    pub fn serialize(&self) -> Vec<u8> {
        let length = (8 + self.payload.len()) as u16;
        let mut out = Vec::with_capacity(length as usize);
        out.extend_from_slice(&self.src_port.to_be_bytes());
        out.extend_from_slice(&self.dst_port.to_be_bytes());
        out.extend_from_slice(&length.to_be_bytes());
        out.extend_from_slice(&0u16.to_be_bytes()); // checksum placeholder
        out.extend_from_slice(&self.payload);
        out
    }
}

/// Stub UDP socket.
pub struct UdpSocket {
    pub local_port: u16,
}

impl UdpSocket {
    pub fn bind(port: u16) -> Self {
        UdpSocket { local_port: port }
    }

    pub fn send_to(
        &self,
        _data: &[u8],
        _dst_port: u16,
    ) -> Result<usize, &'static str> {
        Err("UDP send_to not yet implemented")
    }

    pub fn recv_from(
        &self,
        _buf: &mut [u8],
    ) -> Result<(usize, u16), &'static str> {
        Err("UDP recv_from not yet implemented")
    }
}
