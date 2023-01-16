#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // 在 ARP 表中搜索下一跳 IP 地址对应的 MAC 地址
    const auto &arp_iter = _arp_table.find(next_hop_ip);
    if (arp_iter == _arp_table.end()) {
        // 如果没有找到 MAC 地址，且对该 IP 地址的 MAC 地址的 ARP 请求报文之前没有发送
        // 发送对应的 ARP 请求报文
        if (_waiting_arp_response_ip_addr.find(next_hop_ip) == _waiting_arp_response_ip_addr.end()) {
            // 创建对应的 ARP 请求报文
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ip_address = next_hop_ip;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.target_ethernet_address = {};

            // 封装到以太网帧中发送
            EthernetFrame eth_frame;
            eth_frame.header().src = _ethernet_address;
            eth_frame.header().dst = ETHERNET_BROADCAST;
            eth_frame.header().type = EthernetHeader::TYPE_ARP;
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);

            // 存放该请求和超时时间，防止重复发送 ARP 请求
            _waiting_arp_response_ip_addr[next_hop_ip] = _arp_response_ttl;
        }
        // 将缺乏 MAC 地址无法发送的 IP 数据报和下一跳地址保存
        _waiting_arp_internet_datagrams.push_back({next_hop, dgram});
    } else {
        // 如果找到了 MAC 地址，则直接封装以太网帧并发送
        EthernetFrame eth_frame;
        eth_frame.header().src = _ethernet_address;
        eth_frame.header().dst = arp_iter->second.eth_address;
        eth_frame.header().type = EthernetHeader::TYPE_IPv4;
        eth_frame.payload() = dgram.serialize();
        _frames_out.push(eth_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 如果收到的以太网帧既不是广播帧，目的 MAC 地址也不是端口地址，则直接返回
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    // 如果以太网帧封装的内容是 IP 数据报，则如果能从 payload 中成功解析则返回解析出的 IP 数据报，否则返回空
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        return dgram;
    }

    // 如果以太网帧封装的内容是 ARP 报文
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        // 首先从 payload 中解析出对应的 ARP 报文
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        // 读取报文的源/目的 IP/MAC 地址
        const uint32_t &src_ip_addr = arp_msg.sender_ip_address;
        const uint32_t &dst_ip_addr = arp_msg.target_ip_address;
        const EthernetAddress &src_eth_addr = arp_msg.sender_ethernet_address;
        const EthernetAddress &dst_eth_addr = arp_msg.target_ethernet_address;
        // 判断 ARP 报文是否是有效的 ARP 请求报文 或 ARP 响应报文
        bool valid_request = arp_msg.opcode == ARPMessage::OPCODE_REQUEST && dst_ip_addr == _ip_address.ipv4_numeric();
        bool valid_response = arp_msg.opcode == ARPMessage::OPCODE_REPLY && dst_eth_addr == _ethernet_address;

        // 如果是有效的 ARP 请求报文(请求的目标 IP 地址是端口的 IP 地址)，构建相应的 ARP 响应报文
        // 封装在以太网帧中返回
        if (valid_request) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ip_address = src_ip_addr;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.target_ethernet_address = src_eth_addr;

            EthernetFrame eth_frame;
            eth_frame.header().src = _ethernet_address;
            eth_frame.header().dst = src_eth_addr;
            eth_frame.header().type = EthernetHeader::TYPE_ARP;
            eth_frame.payload() = arp_reply.serialize();
            _frames_out.push(eth_frame);
        }

        // 如果 ARP 报文有效，根据其源 MAC 地址和源 IP 地址更新 ARP 表
        // 从等待目的 MAC 地址的数据报中找到目的 IP 地址和 ARP 报文的源 IP 地址一致的，重新发送
        // 此时因为更新了 ARP 表，所以可以成功发送，并将其从等待列表中删除
        if (valid_request || valid_response) {
            _arp_table[src_ip_addr] = {src_eth_addr, _arp_entry_ttl};
            for (auto iter = _waiting_arp_internet_datagrams.begin(); iter != _waiting_arp_internet_datagrams.end();) {
                if (iter->first.ipv4_numeric() == src_ip_addr) {
                    send_datagram(iter->second, iter->first);
                    iter = _waiting_arp_internet_datagrams.erase(iter);
                } else {
                    iter++;
                }
            }
            _waiting_arp_response_ip_addr.erase(src_ip_addr);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // 更新 ARP 表中的条目的持续时间，删除过期条目
    for (auto iter = _arp_table.begin(); iter != _arp_table.end();) {
        if (iter->second.ttl <= ms_since_last_tick) {
            iter = _arp_table.erase(iter);
        } else {
            iter->second.ttl -= ms_since_last_tick;
            iter++;
        }
    }

    // 更新等待 ARP 响应报文的 IP 地址的持续时间，如果超时，则重新发送一次 ARP 请求报文
    for (auto iter = _waiting_arp_response_ip_addr.begin(); iter != _waiting_arp_response_ip_addr.end();) {
        if (iter->second <= ms_since_last_tick) {
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ip_address = iter->first;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.target_ethernet_address = {};

            EthernetFrame eth_frame;
            eth_frame.header().src = _ethernet_address;
            eth_frame.header().dst = ETHERNET_BROADCAST;
            eth_frame.header().type = EthernetHeader::TYPE_ARP;
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);

            iter->second = _arp_response_ttl;
            iter++;
        } else {
            iter->second -= ms_since_last_tick;
            iter++;
        }
    }
}
