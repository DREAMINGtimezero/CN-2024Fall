#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return total_outstanding_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  // 连续的重传次数
  return total_retransmission_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // Your code here.
  while ( ( window_size_ == 0 ? 1 : window_size_ ) > total_outstanding_ ) {
    if ( FIN_sent_ ) {
      break; //  如果 FIN 已发送则直接结束。
    }

    auto msg { make_empty_message() };

    // 如果还没有发送 SYN 位，先发送 SYN 位
    if ( not SYN_sent_ ) {
      msg.SYN = true;
      SYN_sent_ = true;
    }

    // 计算剩余的窗口大小，避免超出窗口大小
    const uint64_t remaining { ( window_size_ == 0 ? 1 : window_size_ ) - total_outstanding_ };
    const size_t len { min( TCPConfig::MAX_PAYLOAD_SIZE, remaining - msg.sequence_length() ) };
    auto&& payload { msg.payload };

    while ( reader().bytes_buffered() > 0U and payload.size() < len ) {
      string_view view { reader().peek() };
      view = view.substr( 0, len - payload.size() ); // 根据剩余空间截取数据
      payload += view;
      input_.reader().pop( view.size() ); // 从输入流中移除已读取的数据
    }

    // 没有发送 FIN 且剩余窗口可以容纳数据且输入已结束，则发送 FIN 位
    if ( not FIN_sent_ and remaining > msg.sequence_length() and reader().is_finished() ) {
      msg.FIN = true;
      FIN_sent_ = true;
    }

    // 没有有效的负载，直接退出
    if ( msg.sequence_length() == 0 ) {
      break;
    }

    transmit( msg );

    // 启动定时器
    if ( not timer_.is_active() ) {
      timer_.start();
    }

    // 更新发送的绝对序列号和待确认字节数
    next_abs_seqno_ += msg.sequence_length();
    total_outstanding_ += msg.sequence_length();
    outstanding_message_.emplace( move( msg ) );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  // 创建一个空的 TCP 消息
  return { Wrap32::wrap( next_abs_seqno_, isn_ ), false, {}, false, input_.has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  // 接收来自接收方的 TCPReceiverMessage
  if ( input_.has_error() ) {
    return;
  }
  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  window_size_ = msg.window_size; // 更新接收到的窗口大小
  if ( not msg.ackno.has_value() ) {
    return; // 没有需要处理的确认
  }

  const uint64_t recv_ack_abs_seqno { msg.ackno->unwrap( isn_, next_abs_seqno_ ) };
  if ( recv_ack_abs_seqno > next_abs_seqno_ ) {
    return; // 如果收到的 ack 大于当前的序列号，则跳过
  }

  bool has_acknowledgment = false;
  while ( not outstanding_message_.empty() ) {
    const auto& message { outstanding_message_.front() };
    if ( ack_abs_seqno_ + message.sequence_length() > recv_ack_abs_seqno ) {
      break; // 如果当前消息未被完全确认，则跳出循环
    }

    has_acknowledgment = true;
    ack_abs_seqno_ += message.sequence_length();
    total_outstanding_ -= message.sequence_length();
    outstanding_message_.pop(); // 从队列中移除已确认的消息
  }

  if ( has_acknowledgment ) {
    total_retransmission_ = 0;
    timer_.reload( initial_RTO_ms_ ); // 重置定时器和重传次数
    outstanding_message_.empty() ? timer_.stop() : timer_.start();
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // Your code here.
  // 每经过时间（ms_since_last_tick），检查定时器是否超时并进行重传
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) {
    if ( outstanding_message_.empty() ) {
      return;
    }
    transmit( outstanding_message_.front() ); // 重传队列中的第一个message
    if ( window_size_ != 0 ) {
      total_retransmission_ += 1;
      timer_.exponential_backoff(); // 每次重传超时后将 RTO 时间翻倍
    }
    timer_.reset();
  }
}
