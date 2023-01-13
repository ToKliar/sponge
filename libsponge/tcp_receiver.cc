#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();

    // 当包含 SYN 的 Segment 没有到来前，其他的 Segment 都会被遗弃
    // 包含 SYN 的 Segment 到来后，设置 isn = seqno
    if (!_set_syn) {
        if (!header.syn) {
            return;
        }
        _isn = header.seqno;
        _set_syn = true;
    }

    // 根据 bytestream 写出的字节数 + 1（还要计算开头的 SYN），得到 checkpoint，即 Accept 的最后一个字节的 absolute
    // index
    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    // 根据 checkpoint、seqno 和 isn 计算得到 absolute seqno
    uint64_t abs_seqno = unwrap(header.seqno, _isn, abs_ackno);
    // 根据 absolute seqno 计算 stream index
    uint64_t stream_index = abs_seqno - 1 + (header.syn);
    _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // 如果没有任何一个 Segment 被接收，返回 nullopt
    if (!_set_syn) {
        return nullopt;
    }

    // 计算 Accept 的最后一个字节的 absolute seqno，即为 absolute ackno
    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    // 注意到，如果已经传输了 FIN，则最后一个 absolute ackno 需要加上最后一位的 FIN
    if (_reassembler.stream_out().input_ended()) {
        ++abs_ackno;
    }
    // 将 absolute ackno 转换为 seqno
    return WrappingInt32(_isn) + abs_ackno;
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
