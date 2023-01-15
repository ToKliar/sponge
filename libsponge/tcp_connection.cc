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
    bool need_send_ack = seg.length_in_sequence_space();
    
    _receiver.segment_received(seg);

    if (seg.header().rst) {
        end_connection(false);
        return;
    }        
    
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (need_send_ack && !_sender.segments_out().empty()) {
            need_send_ack = false;
        }
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        !_linger_after_streams_finish) {
        _active = false;
        return;
    }

    if (need_send_ack) {
        _sender.send_empty_segment();
    }

    send_segment_out();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segment_out();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        // abort the connection, and send a reset segment to the peer
        _sender.segments_out().pop();
        end_connection(true);
        return;
    }

    send_segment_out();
    _time_since_last_segment_received_ms += ms_since_last_tick;
    // end the connection cleanly if necessary
    

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && 
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        _linger_after_streams_finish && _time_since_last_segment_received_ms >= 10 * _cfg.rt_timeout) {
        _active = _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segment_out();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _active = true;
    send_segment_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            end_connection(false);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::end_connection(bool send_rst) {
    if (send_rst) {
        TCPSegment segment;
        segment.header().rst = true;
        _segments_out.push(segment);
    }
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
    _linger_after_streams_finish = false;
}

//! Send segments in TCP sender out 
void TCPConnection::send_segment_out() {
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
