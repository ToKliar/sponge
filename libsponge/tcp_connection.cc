#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received_ms; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    _time_since_last_segment_received_ms = 0;
    // 对于非空的包（可能是一个 keep-live 包），需要发送 ACK（一个空包）
    bool need_send_ack = seg.length_in_sequence_space();
    
    // Connection 对应的 TCPReceiver 处理收到的 Segment
    _receiver.segment_received(seg);

    // 如果收到的 Segment 中有 RST，则直接（不正常，说明出错了）断开连接（unclean shutdown），不需要发送 RST 包
    if (seg.header().rst) {
        end_connection(false);
        return;
    }        
    
    // 如果收到的包中包含 ACK，首先 TCPSender 根据 ackno 和 win 进行处理，如果处理后 TCPSender 没有需要发送的包，则发送一个空包作为对这个包的 ACK
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (need_send_ack && !_sender.segments_out().empty()) {
            need_send_ack = false;
        }
    }

    // 如果 Receiver 收到了 SYN 包，但是 Sender 并未建立连接（处于 CLOSED 阶段，没有给另一端的 Connection 发送 SYN 包），则建立连接（主动发送 SYN 包）
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }

    // 如果 Receiver 收到了 FIN 包（FIN_RECV 状态），说明此时另一端发送的数据已经被全部收到了
    // 此时 Sender （SYN_ACK 状态，收到了 SYN 包但是没有发送 FIN 包）还有数据需要传输给另一端
    // 此时开始 _linger_after_streams_finish = false，表明此时断开连接对这一端没有损失（所有数据都收到了）
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    // 如果 Receiver 收到了 FIN 包（FIN_RECV 状态），说明此时另一端发送的数据已经被全部收到了
    // 此时 Sender （FIN_ACK 状态，发送了 FIN 包并且收到了 ACK），说明这一端需要发送的数据全部被接收到了
    // _linger_after_streams_finish == false 说明此时断开连接对这一端没有损失
    // 另一端收到了全部数据则断开连接对另一端也没有损失，此时可以正常断开连接（clean shutdown）
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        !_linger_after_streams_finish) {
        _active = false;
        return;
    }

    // 如果需要发送空的 ACK 包，则直接发送
    if (need_send_ack) {
        _sender.send_empty_segment();
    }

    // 实际执行发送包的动作
    send_segment_out();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    // 将要发送的 data 通过 Sender 发送
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segment_out();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    // sender 感知时间变化 
    _sender.tick(ms_since_last_tick);

    // 如果 Sender 的重传次数超过最大次数，则断开连接，发送 RST 包给另一端
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        // 此时将可能重传的包清空
        _sender.segments_out().pop();
        end_connection(true);
        return;
    }

    // 发送可能需要重传的包
    send_segment_out();

    // 更新时间
    _time_since_last_segment_received_ms += ms_since_last_tick;
    
    // 如果 Receiver 收到了 FIN 包（FIN_RECV 状态），说明此时另一端发送的数据已经被全部收到了
    // 此时 Sender （FIN_ACK 状态，发送了 FIN 包并且收到了 ACK），说明这一端需要发送的数据全部被接收到了
    // _linger_after_streams_finish == true 说明收到 FIN 包时已经发送了 FIN 包
    // 此时可能对于收到的 FIN 包的 ACK 包对面无法收到然后重发 FIN 包，所以需要等待一段时间处理另一端重传的 FIN 包
    // 超时后可以断开连接 clean shutdown
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        _linger_after_streams_finish && _time_since_last_segment_received_ms >= 10 * _cfg.rt_timeout) {
        _active = _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    // 关闭输入流，将已经输入的内容中未发送的部分发送出去
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segment_out();
}

void TCPConnection::connect() {
    // 这一端建立到另一端的连接，发送 SYN 包
    _sender.fill_window();
    _active = true;
    send_segment_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // Connection 对象删除时如果连接还处于活跃状态，需要断开 unclean shutdown
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            end_connection(false);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

//! Shutdown connection (unclean)
void TCPConnection::end_connection(bool send_rst) {
    // 如果需要发送 RST 包直接发送
    if (send_rst) {
        TCPSegment segment;
        segment.header().rst = true;
        _segments_out.push(segment);
    }
    // 断开连接，将 Sender 和 Receiver 的流设为 error，并且设置连接状态为 false
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
    _linger_after_streams_finish = false;
}

//! Send segments in TCP sender out 
void TCPConnection::send_segment_out() {
    // 实际上是由 Connection 执行发送动作
    // 从 Sender 中的 segments_out 中按顺序取出要发送的包（如果需要顺便发送 ACK 和 window_size，则附加在包里面），然后放在发送队列中，等待发送
    while (!_sender.segments_out().empty()) {
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
            segment.header().win = _receiver.window_size();
        }
        _segments_out.push(segment);
    }
}
