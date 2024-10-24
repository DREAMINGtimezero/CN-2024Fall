#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  // Your code here.
  // 检查流是否已关闭
  return closed_;
}

void Writer::push( string data )
{
  // Your code here.
  // 检查写入条件：流是否关闭、可用容量是否为0、数据是否为空
  if ( Writer::is_closed() or Writer::available_capacity() == 0 or data.empty() ) {
    return;
  }
  // 如果推送的数据超过可用容量，则截断数据
  if ( data.size() > Writer::available_capacity() ) {
    data.resize( Writer::available_capacity() );
  }
  total_pushed_ += data.size();
  total_buffered_ += data.size();
  // 将数据移动到流中
  stream_.emplace( move( data ) );
}

void Writer::close()
{
  // Your code here.
  // 关闭字节流
  closed_ = true;
  return;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  // 计算并返回可用容量
  return capacity_ - total_buffered_;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  // 返回总共推送的字节数
  return total_pushed_;
}

bool Reader::is_finished() const
{
  // Your code here.
  // 检查字节流是否完成：流是否关闭且没有剩余字节
  return closed_ and total_buffered_ == 0;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  // 返回总共弹出的字节数
  return total_popped_;
}

string_view Reader::peek() const
{
  // Your code here.
  return stream_.empty() ? string_view {} // 如果流为空，返回空的 string_view
                                          // 否则返回缓冲区的前缀（已移除前缀的部分）
                         : string_view { stream_.front() }.substr( removed_prefix_ );
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  // 更新缓冲区字节数
  total_buffered_ -= len;
  total_popped_ += len;
  // 从流中弹出指定字节数
  while ( len != 0U ) {
    const uint64_t& size { stream_.front().size() - removed_prefix_ };
    // 如果要弹出的字节数小于当前块的可用字节数，仅更新前缀
    if ( len < size ) {
      removed_prefix_ += len;
      break; // with len = 0;
    }
    // 否则，弹出当前块并重置前缀
    stream_.pop();
    removed_prefix_ = 0;
    len -= size;
  }
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  // 返回当前缓冲的字节数
  return total_buffered_;
}