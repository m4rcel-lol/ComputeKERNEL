//! TCP layer (Layer 4) — stub implementation.

use alloc::vec::Vec;

bitflags::bitflags! {
    /// TCP control flags.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct TcpFlags: u8 {
        const FIN = 0x01;
        const SYN = 0x02;
        const RST = 0x04;
        const PSH = 0x08;
        const ACK = 0x10;
        const URG = 0x20;
        const ECE = 0x40;
        const CWR = 0x80;
    }
}

/// A TCP segment (simplified — no options).
#[derive(Debug, Clone)]
pub struct TcpSegment {
    pub src_port: u16,
    pub dst_port: u16,
    pub seq_num: u32,
    pub ack_num: u32,
    pub flags: TcpFlags,
    pub window: u16,
    pub payload: Vec<u8>,
}

impl TcpSegment {
    /// Parse a TCP segment from a byte slice.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if data.len() < 20 {
            return None;
        }
        let src_port = u16::from_be_bytes([data[0], data[1]]);
        let dst_port = u16::from_be_bytes([data[2], data[3]]);
        let seq_num = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
        let ack_num = u32::from_be_bytes([data[8], data[9], data[10], data[11]]);
        let data_offset = ((data[12] >> 4) * 4) as usize;
        let flags = TcpFlags::from_bits_truncate(data[13]);
        let window = u16::from_be_bytes([data[14], data[15]]);
        let payload = data[data_offset..].to_vec();
        Some(TcpSegment { src_port, dst_port, seq_num, ack_num, flags, window, payload })
    }
}

/// TCP connection state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TcpState {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
}

/// Stub TCP socket.
pub struct TcpSocket {
    pub state: TcpState,
    pub local_port: u16,
    pub remote_port: u16,
}

impl TcpSocket {
    pub fn new(local_port: u16) -> Self {
        TcpSocket {
            state: TcpState::Closed,
            local_port,
            remote_port: 0,
        }
    }

    pub fn connect(&mut self, _remote_port: u16) -> Result<(), &'static str> {
        Err("TCP connect not yet implemented")
    }

    pub fn send(&self, _data: &[u8]) -> Result<usize, &'static str> {
        Err("TCP send not yet implemented")
    }

    pub fn recv(&self, _buf: &mut [u8]) -> Result<usize, &'static str> {
        Err("TCP recv not yet implemented")
    }
}
