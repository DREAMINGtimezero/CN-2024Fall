#include "tcp_receiver.hh"

using namespace std;

// TCPReceiver 类的 receive 方法
void TCPReceiver::receive( TCPSenderMessage message )
{
  // 检查是否有写入错误
  if ( writer().has_error() ) {
    return;
  }

  // 如果接收到 RST 标志，设置读取器错误并返回
  if ( message.RST ) {
    reader().set_error();
    return;
  }

  // 如果没有设置 zero_point，且接收到的消息中没有 SYN 标志，返回
  if ( not zero_point_.has_value() ) {
    if ( not message.SYN ) {
      return;
    }
    // 如果接收到的消息是 SYN，设置 zero_point 为该消息的序列号
    zero_point_.emplace( message.seqno );
  }

  // 计算绝对序列号
  const uint64_t checkpoint { writer().bytes_pushed() + 1 /* SYN */ }; // 计算期待的负载的绝对序列号
  const uint64_t absolute_seqno { message.seqno.unwrap( zero_point_.value(), checkpoint ) };

  // 计算流索引
  const uint64_t stream_index { absolute_seqno + static_cast<uint64_t>( message.SYN ) - 1 /* SYN */ };

  // 将负载插入重组器
  reassembler_.insert( stream_index, move( message.payload ), message.FIN );
}

// TCPReceiver 类的 send 方法
TCPReceiverMessage TCPReceiver::send() const
{
  // 计算窗口大小，确保不超过 UINT16_MAX
  const uint16_t window_size { writer().available_capacity() > UINT16_MAX
                                 ? static_cast<uint16_t>( UINT16_MAX )
                                 : static_cast<uint16_t>( writer().available_capacity() ) };

  // 如果已经设置 zero_point
  if ( zero_point_.has_value() ) {
    // 计算 ACK 序列号
    const uint64_t ack_for_seqno { writer().bytes_pushed() + 1 + static_cast<uint64_t>( writer().is_closed() ) };
    // 返回一个 TCPReceiverMessage，其中包含确认序列号、窗口大小和错误状态
    return { Wrap32::wrap( ack_for_seqno, zero_point_.value() ), window_size, writer().has_error() };
  }

  return { nullopt, window_size, writer().has_error() };
}
