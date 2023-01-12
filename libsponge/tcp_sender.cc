#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _time_out{retx_timeout}
    , _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _outgoing_size; }

void TCPSender::fill_window() {
    // 初始情况下 windows_size = 0，应该设置为 1 来发送第一个 Segment
    uint64_t curr_windows_size = _window_size ? _window_size : 1;

    // 当有空间传输新的 Segment 时
    while (curr_windows_size > _outgoing_size) {
        // 如果已经发送了最后一个 Segment 就无需重复发送了
        // if (_set_fin) {
        //     break;
        // }

        TCPSegment segment;
        // 如果还没发送第一个 Segment (SYN Segment)，则设置 Segment 的 SYN 标记
        // 发送第一个 Segment 时一定有 _window_size = 1, _outgoing_size = 0s
        if (!_set_syn) {
            _set_syn = true;
            segment.header().syn = true;
        }
        // 设置 Segment 的 seqno
        segment.header().seqno = next_seqno();

        // 计算 payload 的 size，剩余空间 curr_windows_size - _outgoing_size 再减去可能的 SYN
        // 字节，尽可能多传，但是不能超过 MAX_PAYLOAD_SIZE 然后从 Bytestream_In 中读取发送的字节流
        const size_t payload_size =
            min(TCPConfig::MAX_PAYLOAD_SIZE, curr_windows_size - _outgoing_size - (segment.header().syn ? 1 : 0));
        string payload = _stream.read(payload_size);

        // 如果未发送最后一个 Segment (FIN Segment) 且 Bytestream_In 已经读取了所有要发送的字节 且 Segment 的 Payload
        // 中还有位置容纳 FIN 字节，附加 FIN 标志
        if (!_set_fin && _stream.eof() && payload.size() + _outgoing_size < curr_windows_size) {
            _set_fin = true;
            segment.header().fin = true;
        }

        // Segment 设置 Payload
        segment.payload() = Buffer(move(payload));

        // 如果 Segment 的 Payload 为空代表目前没有字节需要发送，则退出
        if (segment.length_in_sequence_space() == 0)
            break;

        // 如果尚未发送 Segment 或者 之前发送的 Segment 都已经 ACK，则重启超时的定时器
        if (_segments_outgoing.empty()) {
            _time_pass = 0;
            _time_out = _initial_retransmission_timeout;
        }

        // 发送 Segment，将其保存在已发送未 ACK 的 Segment 队列中，更新已发送未 ACK 的 Segment 的大小总和，更新
        // _next_seqno
        _outgoing_size += segment.length_in_sequence_space();
        _segments_out.push(segment);
        _segments_outgoing.push(segment);
        _next_seqno += segment.length_in_sequence_space();

        // 如果已经发送了最后一个 Segment 则不用再发送新的 Segment 了
        if (_set_fin) {
            break;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 计算 ack 对应的 abs_seqno
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);

    // 如果 abs_ackno 超过 _next_seqno，说明已经过时，返回
    if (abs_ackno > _next_seqno) {
        return;
    }

    while (!_segments_outgoing.empty()) {
        TCPSegment segment = _segments_outgoing.front();
        uint64_t segment_abs_seqno = unwrap(segment.header().seqno, _isn, _next_seqno);
        // 如果有已发送但是未 ACK 的 Segment 的 abs_seqno 在 abs_ackno 之前，说明这个 Segment
        // 已经被接收了，可以从队列中删除 同时重新设置超时时间并重启超时定时器
        if (segment_abs_seqno + segment.length_in_sequence_space() <= abs_ackno) {
            _outgoing_size -= segment.length_in_sequence_space();
            _segments_outgoing.pop();
            _time_out = _initial_retransmission_timeout;
            _time_pass = 0;
        } else {
            break;
        }
    }

    // 更新连续重传次数
    _consecutive_retransmissions = 0;

    // 收到 ack 后更新 window_size 重新发送包
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _time_pass += ms_since_last_tick;

    // 如果定时器超时，且存在已发送未 ACK 的 Segment，重传 seqno 最小的 Segment（队列中第一个 Segment）并重启定时器
    if (_time_pass >= _time_out && !_segments_outgoing.empty()) {
        TCPSegment segment = _segments_outgoing.front();
        // 如果此时 window_size > 0，说明出现网络拥堵，增加连续重传次数，将超时时间加倍
        if (_window_size > 0) {
            _time_out *= 2;
        }
        _time_pass = 0;
        _consecutive_retransmissions++;
        _segments_out.push(segment);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().ackno = next_seqno();
    _segments_out.push(segment);
}
