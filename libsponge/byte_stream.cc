#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _queue(), _capacity_size(capacity), _write_size(0), _read_size(0), _end_input(false), _error(false) {}

size_t ByteStream::write(const string &data) {
    if (_end_input) {
        return 0;
    }
    size_t write_size = min(data.size(), _capacity_size - _queue.size());
    _write_size += write_size;
    for (size_t i = 0; i < write_size; i++) {
        _queue.push_back(data[i]);
    }
    return write_size;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t pop_size = min(len, _queue.size());
    return string(_queue.begin(), _queue.begin() + pop_size);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_size = min(len, _queue.size());
    _read_size += len;
    for (size_t i = 0; i < pop_size; ++i) {
        _queue.pop_front();
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string data = this->peek_output(len);
    this->pop_output(len);
    return data;
}

void ByteStream::end_input() { _end_input = true; }

bool ByteStream::input_ended() const { return _end_input; }

size_t ByteStream::buffer_size() const { return _queue.size(); }

bool ByteStream::buffer_empty() const { return _queue.empty(); }

bool ByteStream::eof() const { return _end_input && _queue.empty(); }

size_t ByteStream::bytes_written() const { return _write_size; }

size_t ByteStream::bytes_read() const { return _read_size; }

size_t ByteStream::remaining_capacity() const { return _capacity_size - _queue.size(); }
