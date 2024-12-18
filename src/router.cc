#include "router.hh"

#include <iostream>
#include <limits>
#include <bit>
#include <cstddef>
#include <optional>
#include <ranges>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";
// 将路由前缀按位右移，确保最长前缀匹配,使用前缀长度确定路由表项的键，并将该路由条目存储到路由表中
   routing_table_[prefix_length][rotr( route_prefix, 32 - prefix_length )] = { interface_num, next_hop };
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
   // 遍历所有网络接口
   for ( const auto& interface : _interfaces ) {
    auto&& datagrams_received { interface->datagrams_received() };
    while ( not datagrams_received.empty() ) {
       // 获取队列中的第一个数据报
      InternetDatagram datagram { move( datagrams_received.front() ) };
      datagrams_received.pop();

 // 如果数据报的TTL小于等于1，则跳过该数据报
      if ( datagram.header.ttl <= 1 ) {
        continue;
      }
  
       // TTL减1，并重新计算校验和
      datagram.header.ttl -= 1;
      datagram.header.compute_checksum();

      const optional<info>& mp { match( datagram.header.dst ) };
      if ( not mp.has_value() ) {
        continue;
      }
      const auto& [num, next_hop] { mp.value() };
       // 将数据报发送到匹配的接口，如果有下一跳地址则使用它，否则使用数据报的目的地址
      _interfaces[num]->send_datagram( datagram,
                                       next_hop.value_or( Address::from_ipv4_numeric( datagram.header.dst ) ) );
    }
  }
}

[[nodiscard]] auto Router::match( uint32_t addr ) const noexcept -> optional<info>
{
  // 使用ranges库中的过滤器，过滤出路由表中匹配地址前缀的项
  auto adaptor = views::filter( [&addr]( const auto& mp ) { return mp.contains( addr >>= 1 ); } )
                 | views::transform( [&addr]( const auto& mp ) -> info { return mp.at( addr ); } );
    // 对路由表进行逆向遍历，并应用过滤器，只取出第一个匹配的路由
  auto res { routing_table_ | views::reverse | adaptor | views::take( 1 ) }; // just kidding
 // 如果没有匹配的路由，返回 nullopt；否则返回匹配到的路由信息
  return res.empty() ? nullopt : optional<info> { res.front() };
}
