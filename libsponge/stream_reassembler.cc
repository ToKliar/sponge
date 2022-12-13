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
    // 此时的 map 是若干个不重叠的序列，key是序列中第一个字节在字节流中的序号，每个序列的 key 都 >= _next_assembled_idx

    /**
     *  第一步是去除新插入的序列中前半部分重叠的部分
     *  存在两种情况：
     *  1. 新的序列刚好可以被传输，即 index <= _next_assembled_idx < index + data.size()，此时注意到序号
     * _next_assembled_idx 前的字节都已经传输了，因此新的序列在 _next_assembled_idx 之前的字节需要被截断
     *  2. 新的序列的开头部分与 map 中某个序列重叠，即 entry.key <= index && index < entry.key +
     * entry.value.size()，此时需要把新的序列中的重叠部分去除，确保 map 中存储的所有序列都是不重叠的
     */

    /**
     *  通过 upper_bound 找到 key 大于 index 的最小的 iter 存在以下情况
     *  1. iter == map.end() 所有的序列的 key 都小于等于 index，此时实际上不会发生截断
     *  2. iter == map.begin() 所有的序列的 key 都大于 index，此时看 index 是否满足情况 1(可能不满足)，如果满足情况 1
     * 则截断前半部分
     *  3. 存在小于等于 index 的序列，此时根据距离 index 最近的序列(pos_iter-1)，截断前半部分
     */
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

    // 截断操作，注意到 new_idx 可能大于 index + data.size()，对应的情况是情况 2 中 key <= index
    // 最近的序列完全把新的序列包含 即存在 entry.key <= index && entry.key + entry.value.size() > index + data.size()
    // 因此用 ssize_t 类型确保不会上溢
    const size_t data_start_pos = new_idx - index;
    ssize_t data_size = data.size() - data_start_pos;

    /**
     *  第二步是截断新序列的后半部分，此时已经截断了前半部分，所以使用 new_idx
     *  对于 key >= new_idx 的所有序列，按照 key 从小到大遍历，存在以下情况
     *  1. 该序列被新序列覆盖，即 new_idx <= entry.key && new_idx + data_size >= entry.key +
     * entry.value.size()，此时将这个 entry 从 map 中删除并继续遍历
     *  2. 这个序列和新序列部分重叠，即 new_idx <= entry.key && new_idx + data_size < entry.key +
     * entry.value.size()，此时在新序列中删除重叠部分，更新 data_size，结束遍历
     *  3. 没有重叠部分，结束遍历
     */
    pos_iter = _unassemble_strs.lower_bound(new_idx);

    while (pos_iter != _unassemble_strs.end() && new_idx <= pos_iter->first) {
        const size_t data_end_pos = new_idx + data_size;
        if (pos_iter->first < data_end_pos) {
            // 情况 2
            if (data_end_pos < pos_iter->first + pos_iter->second.size()) {
                data_size = pos_iter->first - new_idx;
                break;
            }
            // 情况 1
            else {
                _unassemble_bytes_num -= pos_iter->second.size();
                pos_iter = _unassemble_strs.erase(pos_iter);
                continue;
            }
        } else {
            // 情况 3
            break;
        }
    }

    // 之后判断 capacity 是否足够容纳截断后的新序列，如果不够则将新序列舍弃
    size_t first_unaccept_idx = _next_assembled_idx + _capacity - _output.buffer_size();
    if (first_unaccept_idx <= new_idx)
        return;

    /**
     *  新的序列存在未发送且 map 中不包含的部分，即前后截断后的序列长度 > 0
     *  1. 如果截断后可以被发送，new_idx == _next_assembled_idx，则直接发送，未接收的部分存储在 map 中
     *  2. 如果无法发送，存储在 map 中
     */
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

    /**
     *  完成上述工作后发送对 map 中可以被发送的序列
     *  可能因为1. 发送了新序列导致 _next_assemble_idx 变大，2. bystream 中有空间发送新的序列
     *  按照 key 的大小遍历 map
     *  1. 如果 key ==
     * _next_assembled_idx，发送序列，如果全部发送成功继续遍历，如果部分发送成功，删除旧序列，插入未发送成功的那部分，结束遍历
     *  2. 如果 key < _next_assembled_idx，结束遍历
     */
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

    // 如果有 eof 标志，更新 eof 对应的字节序号
    if (eof) {
        _eof_idx = index + data.size();
    }

    // 如果 eof 的字节序号 <= _next_assembled_idx，不再需要发送新的字节，关闭 bytestream 的输入
    if (_eof_idx <= _next_assembled_idx) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassemble_bytes_num; }

bool StreamReassembler::empty() const { return _unassemble_bytes_num == 0; }
