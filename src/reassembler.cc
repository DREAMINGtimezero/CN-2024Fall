#include "reassembler.hh"

using namespace std;

#include <algorithm>
#include <ranges>

// 将数据缓冲区拆分为已组装和未组装的部分
auto Reassembler::split( uint64_t pos ) noexcept
{
  // 查找在指定位置的元素（如果存在）
  auto it { buf_.lower_bound( pos ) };

  // 如果找到的元素恰好在 pos 处，直接返回该元素
  if ( it != buf_.end() && it->first == pos ) {
    return it;
  }

  // 如果 buf_ 为空，返回 buf_.begin() == buf_.end()
  if ( it == buf_.begin() ) {
    return it;
  }

  // 如果前一个元素的结束位置大于 pos，则需要拆分
  if ( const auto pit { prev( it ) }; pit->first + size( pit->second ) > pos ) {
    // 在 it 位置插入新元素，并更新前一个元素的大小
    const auto res = buf_.emplace_hint( it, pos, pit->second.substr( pos - pit->first ) );
    pit->second.resize( pos - pit->first );
    return res;
  }

  return it;
}

// 插入数据到重组器
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // 尝试关闭输出流的辅助 lambda 函数
  const auto try_close = [&]() noexcept -> void {
    if ( end_index_.has_value() && end_index_.value() == writer().bytes_pushed() ) {
      output_.writer().close(); // 关闭输出流
    }
  };

  if ( data.empty() ) {
    if ( !end_index_.has_value() && is_last_substring ) {
      end_index_.emplace( first_index );
    }
    return try_close();
  }

  // 如果写入器已关闭或没有可用容量
  if ( writer().is_closed() || writer().available_capacity() == 0U ) {
    return; // 直接返回
  }

  const uint64_t unassembled_index { writer().bytes_pushed() };
  const uint64_t unacceptable_index { unassembled_index + writer().available_capacity() };

  // 检查索引范围
  if ( first_index + size( data ) <= unassembled_index || first_index >= unacceptable_index ) {
    return; // 超出范围
  }

  // 如果数据超出不可接受索引，调整数据大小
  if ( first_index + size( data ) > unacceptable_index ) {
    data.resize( unacceptable_index - first_index );
    is_last_substring = false; // 设置为 false，避免误判
  }

  // 如果数据开始于未组装的索引之前，移除已缓存的字节
  if ( first_index < unassembled_index ) {
    data.erase( 0, unassembled_index - first_index );
    first_index = unassembled_index; // 更新起始索引
  }

  // 更新结束索引
  if ( !end_index_.has_value() && is_last_substring ) {
    end_index_.emplace( first_index + size( data ) );
  }

  const auto upper { split( first_index + size( data ) ) }; // 找到数据结束位置
  const auto lower { split( first_index ) };                // 找到数据起始位置

  // 更新待处理数据总大小
  ranges::for_each( ranges::subrange( lower, upper ) | views::values,
                    [&]( const auto& str ) { total_pending_ -= str.size(); } );
  total_pending_ += size( data );

  // 将新的数据插入到缓冲区
  buf_.emplace_hint( buf_.erase( lower, upper ), first_index, move( data ) );

  // 处理已组装的数据
  while ( !buf_.empty() ) {
    auto&& [index, payload] { *buf_.begin() }; // 获取缓冲区的第一个元素
    if ( index != writer().bytes_pushed() ) {
      break; // 如果未达到写入器的字节数，退出
    }

    total_pending_ -= size( payload ); // 更新待处理数据大小
    output_.writer().push( move( payload ) );
    buf_.erase( buf_.begin() );
  }

  return try_close();
}

uint64_t Reassembler::bytes_pending() const
{
  // 返回总待处理字节数
  return total_pending_;
}
