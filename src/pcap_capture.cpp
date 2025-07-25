#include <pcap.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <memory>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "dns_parse.h"
#include "pcap_capture.h"

static pcap_t* global_handle = nullptr;

std::vector<std::string> parse_dns_packet(const u_char* packet, uint32_t ip_header_len, uint32_t captured_len) {
    std::vector<std::string> domains;

    if (captured_len < ETHERNET_HEADER_LEN + ip_header_len + sizeof(struct udphdr)) return domains;

    const struct udphdr* udp_header = reinterpret_cast<const struct udphdr*>(packet + ETHERNET_HEADER_LEN + ip_header_len);
    uint16_t udp_len = ntohs(udp_header->uh_ulen);
    if (udp_len < 8) return domains;

    uint32_t udp_payload_offset = ETHERNET_HEADER_LEN + ip_header_len + sizeof(struct udphdr);
    uint32_t udp_payload_len = captured_len > udp_payload_offset ? captured_len - udp_payload_offset : 0;

    if ((udp_len - 8) > udp_payload_len) return domains;

    const uint8_t* dns_data = packet + udp_payload_offset;
    uint32_t dns_len = udp_len - 8;
    std::vector<uint8_t> dns_packet(dns_data, dns_data + dns_len);

    return extract_dns_queries(dns_packet);
}

void packet_handler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet) {
    try {
        if (header->caplen < ETHERNET_HEADER_LEN + sizeof(struct ip)) return;

        const struct ip* ip_header = reinterpret_cast<const struct ip*>(packet + ETHERNET_HEADER_LEN);
        if (ip_header->ip_v == 6) return;
        if (ip_header->ip_p != IPPROTO_UDP) return;

        uint32_t ip_header_len = ip_header->ip_hl << 2;
        const struct udphdr* udp_header = reinterpret_cast<const struct udphdr*>(packet + ETHERNET_HEADER_LEN + ip_header_len);

        if (ntohs(udp_header->uh_sport) != DNS_PORT && ntohs(udp_header->uh_dport) != DNS_PORT) return;

        std::vector<std::string> domains = parse_dns_packet(packet, ip_header_len, header->caplen);
        for (const auto& domain : domains) {
            if (!domain.empty()) {
                domain_queue.push(domain);
                // std::cout << "[dns_capture] domain: " << domain << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[dns_capture] Exception: " << e.what() << "\n";
    }
}

void start_packet_capture(const std::string& dev) {
    char errbuf[PCAP_ERRBUF_SIZE];
    global_handle = pcap_open_live(dev.c_str(), 65535, 1, 1000, errbuf);
    if (!global_handle) {
        std::cerr << "[dns_capture] pcap_open_live failed: " << errbuf << "\n";
        return;
    }

    struct bpf_program fp;
    const char filter_exp[] = "udp and port 53";

    if (pcap_compile(global_handle, &fp, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        std::cerr << "[dns_capture] pcap_compile failed: " << pcap_geterr(global_handle) << "\n";
        pcap_close(global_handle);
        global_handle = nullptr;
        return;
    }

    if (pcap_setfilter(global_handle, &fp) == -1) {
        std::cerr << "[dns_capture] pcap_setfilter failed: " << pcap_geterr(global_handle) << "\n";
        pcap_freecode(&fp);
        pcap_close(global_handle);
        global_handle = nullptr;
        return;
    }

    std::cout << "[dns_capture] start capturing on: " << dev << "\n";

    while (!stop_processing.load(std::memory_order_acquire)) {
        struct pcap_pkthdr* header;
        const u_char* packet;
        int res = pcap_next_ex(global_handle, &header, &packet);

        if (res == 1) {
            packet_handler(nullptr, header, packet);
        } else if (res == -1) {
            std::cerr << "[dns_capture] error reading packet: " << pcap_geterr(global_handle) << "\n";
            break;
        }
    }

    pcap_freecode(&fp);
    pcap_close(global_handle);
    global_handle = nullptr;
    std::cout << "[dns_capture] packet capture stopped\n";
}

void stop_packet_capture() {
    if (global_handle) {
        pcap_breakloop(global_handle);
    }
}