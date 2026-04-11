//! TCP/IP network stack stubs.
//!
//! Provides Ethernet, IP, TCP, and UDP layer stubs.

pub mod ethernet;
pub mod ip;
pub mod tcp;
pub mod udp;

pub use ethernet::EthernetFrame;
pub use ip::IpPacket;
pub use tcp::TcpSegment;
pub use udp::UdpDatagram;
