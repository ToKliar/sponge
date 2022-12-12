#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _unassemble_strs()
    , _next_assembled_idx(0)
    , _unassemble_bytes_num(0)
    , _eof_idx(-1)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    auto pos_iter = _unassemble_strs.upper_bound(index);
    if (pos_iter != _unassemble_strs.begin()) {
        pos_iter--;
    }

    size_t new_idx = index;

    if (pos_iter != _unassemble_strs.end() && pos_iter->first <= index) {
        const size_t up_idx = pos_iter->first;

        if (index < up_idx + pos_iter->second.size()) {
            new_idx = up_idx + pos_iter->second.size();
        }
    } else if (index < _next_assembled_idx) {
        new_idx = _next_assembled_idx;
    }

    const size_t data_start_pos = new_idx - index;
    ssize_t data_size = data.size() - data_start_pos;

    pos_iter = _unassemble_strs.lower_bound(new_idx);

    while (pos_iter != _unassemble_strs.end() && new_idx <= pos_iter->first) {
        const size_t data_end_pos = new_idx + data_size;
        if (pos_iter->first < data_end_pos) {
            if (data_end_pos < pos_iter->first + pos_iter->second.size()) {
                data_size = pos_iter->first - new_idx;
                break;
            } else {
                _unassemble_bytes_num -= pos_iter->second.size();
                pos_iter = _unassemble_strs.erase(pos_iter);
                continue;
            }
        } else {
            break;
        }
    }

    size_t first_unaccept_idx = _next_assembled_idx + _capacity - _output.buffer_size();
    if (first_unaccept_idx <= new_idx)
        return;

    if (data_size > 0) {
        const string new_data = data.substr(data_start_pos, data_size);
        if (new_idx == _next_assembled_idx) {
            const size_t write_bytes = _output.write(new_data);
            _next_assembled_idx += write_bytes;
            if (write_bytes < new_data.size()) {
                const string store_data = new_data.substr(write_bytes, new_data.size() - write_bytes);
                _unassemble_bytes_num += store_data.size();
                _unassemble_strs.insert(make_pair(_next_assembled_idx, std::move(store_data)));
            }
        } else {
            const string store_data = new_data.substr(0, new_data.size());
            _unassemble_bytes_num += store_data.size();
            _unassemble_strs.insert(make_pair(new_idx, std::move(store_data)));
        }
    }

    for (auto iter = _unassemble_strs.begin(); iter != _unassemble_strs.end();) {
        if (iter->first == _next_assembled_idx) {
            const size_t write_bytes = _output.write(iter->second);
            _next_assembled_idx += write_bytes;
            if (write_bytes < iter->second.size()) {
                _unassemble_bytes_num += iter->second.size() - write_bytes;
                _unassemble_strs.insert(make_pair(_next_assembled_idx, std::move(iter->second.substr(write_bytes))));

                _unassemble_bytes_num -= iter->second.size();
                _unassemble_strs.erase(iter);
                break;
            }
            _unassemble_bytes_num -= iter->second.size();
            iter = _unassemble_strs.erase(iter);
        } else {
            break;
        }
    }

    if (eof) {
        _eof_idx = index + data.size();
    }

    //! If Eof Index less than next_assemble_index, Don't need input any more
    if (_eof_idx <= _next_assembled_idx) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassemble_bytes_num; }

bool StreamReassembler::empty() const { return _unassemble_bytes_num == 0; }
