#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
	
	// 直接向路由表中添加条目	
	_route_table.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
	// 读取数据报中的目标 IP 地址
    const uint32_t dst_ip_addr = dgram.header().dst;
    auto match_entry = _route_table.end();

	// 进行最长前缀匹配，选出其中前缀长度最长的转发表条目
    for (auto iter = _route_table.begin(); iter != _route_table.end(); iter++) {
        if (iter->prefix_length == 0 || (iter->route_prefix ^ dst_ip_addr) >> (32 - iter->prefix_length) == 0) {
            if (match_entry == _route_table.end() || match_entry->prefix_length < iter->prefix_length) {
                match_entry = iter;
            }
        }
    }

	// 如果匹配了某一转发表条目，并且数据报的 ttl > 1（确保可以继续转发） 
	// 使用对应的输出端口进行转发，注意到转发表条目中下一跳地址可能为空，需要根据目标 IP 地址构建
    if (match_entry != _route_table.end() && dgram.header().ttl-- > 1) {
        const optional<Address> next_hop = match_entry->next_hop;
        AsyncNetworkInterface &interface = _interfaces[match_entry->interface_idx];
        if (next_hop.has_value()) {
            interface.send_datagram(dgram, next_hop.value());
        } else {
            interface.send_datagram(dgram, Address::from_ipv4_numeric(dst_ip_addr));
        }
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
