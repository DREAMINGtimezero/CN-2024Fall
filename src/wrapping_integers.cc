#include "wrapping_integers.hh"

using namespace std;

// 定义 Wrap32 类的静态方法 wrap
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  /*n = [ high_32 | low_32 ]
    wrap(n) = low_32 + zero_point.raw_value_
          or low_32 + zero_point.raw_value_ - 2^{32}
          */
  // 将输入的 n（无符号64位整数）取模到32位范围内。
  // 通过将 n 转换为 uint32_t（32位无符号整数）并加上起始点来实现。
  return zero_point + static_cast<uint32_t>( n );
}

// 定义 Wrap32 类的成员方法 unwrap
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // 计算原始值与零点的差值
  const uint64_t n_low32 { this->raw_value_ - zero_point.raw_value_ };
  // 获取 checkpoint 的低32位
  const uint64_t c_low32 { checkpoint & MASK_LOW_32 };
  // 组合 checkpoint 的高32位和 n_low32 得到结果
  const uint64_t res { ( checkpoint & MASK_HIGH_32 ) | n_low32 };

  // 检查 res 是否超过 BASE，并根据低32位的值调整
  if ( res >= BASE && n_low32 > c_low32 && ( n_low32 - c_low32 ) > ( BASE / 2 ) ) {
    return res - BASE; // 如果 res 太大，进行回调
  }
  // 检查 res 是否在低位
  if ( res < MASK_HIGH_32 && c_low32 > n_low32 && ( c_low32 - n_low32 ) > ( BASE / 2 ) ) {
    return res + BASE; // 如果 res 太小，进行取模
  }
  return res;
}
