#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // 将 abs_seqno 和 isn 相加后取低 32 位转换为 seqno
    return WrappingInt32{isn + static_cast<uint32_t>(n)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // 将 seqno 和 isn 相减，因为是无符号整数减法可以直接得到 abs_seqno 的低32位
    uint32_t seqno = n.raw_value();
    uint32_t isnno = isn.raw_value();
    uint64_t abs_seqno = static_cast<uint64_t>(seqno - isnno);

    /**
     *  存在两种情况
     *  1. abs_seqno >= checkpoint，此时 abs_seqno 就是最终的结果
     *  2. abs_seqno < checkpoint，此时 abs_seqno + n * 2^32 <= checkpoint <= abs_seqno + (n + 1) * 2^32
     * real_abs_seqno = abs_seqno + n * 2^32 | abs_seqno + (n + 1) * 2^32
     * n = (checkpoint - abs_seqno) >> 32，此时比较 两个可能的值
     */
    if (checkpoint > abs_seqno) {
        uint64_t diff = checkpoint - abs_seqno;
        uint64_t low = diff & 0xffffffff;
        abs_seqno += (diff >> 32) << 32;
        if (low >= 0x80000000) {
            abs_seqno += 0x100000000;
        }
    }
    return abs_seqno;
}
