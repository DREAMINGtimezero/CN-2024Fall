#include <iostream>
#include <ranges>
#include <utility>
#include <vector>

#include "address.hh"
#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"
#include "parser.hh"

using namespace std;

auto NetworkInterface::make_arp( const uint16_t opcode,
                                 const EthernetAddress& target_ethernet_address,
                                 const uint32_t target_ip_address ) const noexcept -> ARPMessage
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = ethernet_address_;
  arp.sender_ip_address = ip_address_.ipv4_numeric();
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = target_ip_address;
  return arp;
}

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
// 初始化网络接口，设置以太网和 IP 地址
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
// 发送 IPv4 数据报
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const AddressNumeric next_hop_numeric { next_hop.ipv4_numeric() };

  if ( ARP_cache_.contains( next_hop_numeric ) ) {
    const EthernetAddress& dst { ARP_cache_[next_hop_numeric].first }; // 获取目标以太网地址
    return transmit(
      { { dst, ethernet_address_, EthernetHeader::TYPE_IPv4 }, serialize( dgram ) } ); // 发送以太网帧
  }
  // 如果 ARP 缓存中没有目标地址，先将数据报保存到等待队列
  dgrams_waitting_[next_hop_numeric].emplace_back( dgram );

  // 如果该 IP 地址已存在 ARP 响应等待定时器，则不需要重复发送 ARP 请求
  if ( waitting_timer_.contains( next_hop_numeric ) ) {
    return;
  }

  // 否则，创建定时器并发送 ARP 请求
  waitting_timer_.emplace( next_hop_numeric, NetworkInterface::Timer {} );
  const ARPMessage arp_request { make_arp( ARPMessage::OPCODE_REQUEST, {}, next_hop_numeric ) };
  transmit( { { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP }, serialize( arp_request ) } );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // 如果目的地址既不是本接口的以太网地址，也不是广播地址，则丢弃该帧
  if ( frame.header.dst != ethernet_address_ and frame.header.dst != ETHERNET_BROADCAST ) {
    return;
  }

  // 如果帧类型为 IPv4，则解析并将数据报加入接收队列
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram ipv4_datagram;
    if ( parse( ipv4_datagram, frame.payload ) ) {
      datagrams_received_.emplace( move( ipv4_datagram ) );
    }
    return;
  }

  // 如果帧类型为 ARP，则解析 ARP 消息并处理
  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage msg;
    if ( not parse( msg, frame.payload ) ) {
      return;
    }

    const AddressNumeric sender_ip { msg.sender_ip_address };
    const EthernetAddress sender_eth { msg.sender_ethernet_address };
    ARP_cache_[sender_ip] = { sender_eth, Timer {} };

    // 如果是 ARP 请求并且目标 IP 地址是本接口的 IP 地址，则发送 ARP 回复
    if ( msg.opcode == ARPMessage::OPCODE_REQUEST and msg.target_ip_address == ip_address_.ipv4_numeric() ) {
      const ARPMessage arp_reply { make_arp( ARPMessage::OPCODE_REPLY, sender_eth, sender_ip ) };
      transmit( { { sender_eth, ethernet_address_, EthernetHeader::TYPE_ARP }, serialize( arp_reply ) } );
    }

    // 如果有等待该 IP 地址的待发送数据报，发送这些数据报
    if ( dgrams_waitting_.contains( sender_ip ) ) {
      for ( const auto& dgram : dgrams_waitting_[sender_ip] ) {
        transmit( { { sender_eth, ethernet_address_, EthernetHeader::TYPE_IPv4 }, serialize( dgram ) } );
      }
      dgrams_waitting_.erase( sender_ip );
      waitting_timer_.erase( sender_ip );
    }
  }
}

// 定时器更新计时器并清理过期条目
//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  erase_if( ARP_cache_, [&]( auto&& item ) noexcept -> bool {
    return item.second.second.tick( ms_since_last_tick ).expired( ARP_ENTRY_TTL_ms );
  } );

  erase_if( waitting_timer_, [&]( auto&& item ) noexcept -> bool {
    return item.second.tick( ms_since_last_tick ).expired( ARP_RESPONSE_TTL_ms );
  } );
}
