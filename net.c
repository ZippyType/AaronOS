/**
 * =============================================================================
 * AARONOS NETWORK SUBSYSTEM (net.c)
 * =============================================================================
 * VERSION: 1.2.0
 * HARDWARE: Realtek RTL8139 Fast Ethernet
 * PROTOCOLS: Ethernet, ARP, IPv4, ICMP, TCP (Handshake + Data)
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include "io.h"

/* RTL8139 Hardware Register Offsets */
#define REG_MAC          0x00
#define REG_TXSTATUS0    0x10
#define REG_TXADDR0      0x20
#define REG_RXBUF        0x30
#define REG_CMD          0x37
#define REG_CAPR         0x38
#define REG_IMR          0x3C
#define REG_ISR          0x3E
#define REG_RCR          0x44
#define REG_CONFIG1      0x52

/* External Functions in kernel.c */
extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void itoa(int num, char* str, int base);
extern void kmemcpy(void* dest, const void* src, size_t len);

/* Hardware Buffers (Must be aligned for DMA) */
uint8_t rx_buffer[8192 + 16 + 1500] __attribute__((aligned(4)));
uint8_t tx_buffer[4][1536] __attribute__((aligned(4)));
uint8_t tx_cur = 0;

/* Networking State Global Variables */
uint32_t net_io_addr = 0;
uint8_t my_mac[6];
uint8_t my_ip[4] = {10, 0, 2, 15};             // QEMU Default Gateway expects this
uint8_t router_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02}; // QEMU SLIRP Gateway

/* TCP Session State */
uint32_t tcp_seq = 0x55AA1234;
uint32_t tcp_ack = 0;
int tcp_state = 0;                             // 0=Closed, 1=SYN_SENT, 2=ESTABLISHED
char browser_buffer[2048]; 
int browser_buffer_ptr = 0;
int browser_ready = 0;

/* ========================================================================== */
/* 1. PROTOCOL STRUCTURE DEFINITIONS                                          */
/* ========================================================================== */

typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];
    uint8_t  dest_mac[6];
    uint8_t  dest_ip[4];
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    uint8_t  v_ihl;
    uint8_t  tos;
    uint16_t len;
    uint16_t id;
    uint16_t off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t chk;
    uint8_t  src_ip[4];
    uint8_t  dest_ip[4];
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t chk;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint16_t flags;
    uint16_t window;
    uint16_t chk;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

/* TCP Pseudo Header for checksumming */
typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t proto;
    uint16_t tcp_len;
} __attribute__((packed)) tcp_pseudo_hdr_t;

/* ========================================================================== */
/* 2. NETWORK UTILITIES                                                       */
/* ========================================================================== */

uint16_t htons(uint16_t val) { 
    return (val << 8) | (val >> 8); 
}

uint32_t htonl(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val & 0xFF0000) >> 8) | ((val >> 24) & 0xFF);
}

uint16_t net_checksum(void* vdata, size_t length) {
    uint16_t* data = (uint16_t*)vdata;
    uint32_t sum = 0;
    for (; length > 1; length -= 2) sum += *data++;
    if (length == 1) sum += *(uint8_t*)data;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

uint16_t tcp_checksum(ip_hdr_t* ip, tcp_hdr_t* tcp, uint8_t* data, int data_len) {
    tcp_pseudo_hdr_t ph;
    ph.src_ip = *(uint32_t*)ip->src_ip;
    ph.dest_ip = *(uint32_t*)ip->dest_ip;
    ph.zero = 0;
    ph.proto = 6;
    ph.tcp_len = htons(sizeof(tcp_hdr_t) + data_len);

    uint32_t sum = 0;
    uint16_t* p = (uint16_t*)&ph;
    for(int i=0; i<6; i++) sum += p[i];
    p = (uint16_t*)tcp;
    for(int i=0; i<10; i++) sum += p[i];
    p = (uint16_t*)data;
    for(int i=0; i < (data_len/2); i++) sum += p[i];
    if(data_len % 2) sum += (uint16_t)data[data_len-1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ========================================================================== */
/* 3. TRANSMISSION ENGINE                                                     */
/* ========================================================================== */

void net_send_tcp(uint8_t* dest_ip, uint16_t dest_port, uint16_t flags, uint8_t* data, uint16_t data_len) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    ip_hdr_t* ip = (ip_hdr_t*)(buffer + 14);
    tcp_hdr_t* tcp = (tcp_hdr_t*)(buffer + 34);

    for(int i=0; i<6; i++) { eth->dest_mac[i] = router_mac[i]; eth->src_mac[i] = my_mac[i]; }
    eth->type = htons(0x0800);

    ip->v_ihl = 0x45; ip->tos = 0; ip->len = htons(40 + data_len); ip->id = htons(1);
    ip->off = 0; ip->ttl = 64; ip->proto = 6;
    for(int i=0; i<4; i++) { ip->src_ip[i] = my_ip[i]; ip->dest_ip[i] = dest_ip[i]; }
    ip->chk = 0; ip->chk = net_checksum(ip, 20);

    tcp->src_port = htons(12345);
    tcp->dest_port = htons(dest_port);
    tcp->seq = htonl(tcp_seq);
    tcp->ack = htonl(tcp_ack);
    tcp->flags = htons(flags | (5 << 12));
    tcp->window = htons(8192);
    tcp->chk = 0;
    tcp->urgent = 0;
    
    if(data_len > 0) kmemcpy(buffer + 54, data, data_len);
    tcp->chk = tcp_checksum(ip, tcp, buffer + 54, data_len);

    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 14 + 40 + data_len);
    tx_cur = (tx_cur + 1) % 4;
}

void net_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    ip_hdr_t* ip = (ip_hdr_t*)(buffer + 14);
    icmp_hdr_t* icmp = (icmp_hdr_t*)(buffer + 34);

    for(int i=0; i<6; i++) { eth->dest_mac[i] = router_mac[i]; eth->src_mac[i] = my_mac[i]; }
    eth->type = htons(0x0800);

    ip->v_ihl = 0x45; ip->len = htons(28); ip->id = htons(1);
    ip->off = 0; ip->ttl = 64; ip->proto = 1; 
    for(int i=0; i<4; i++) ip->src_ip[i] = my_ip[i];
    ip->dest_ip[0]=ip0; ip->dest_ip[1]=ip1; ip->dest_ip[2]=ip2; ip->dest_ip[3]=ip3;
    ip->chk = 0; ip->chk = net_checksum(ip, 20);

    icmp->type = 8; // Request
    icmp->code = 0;
    icmp->id = htons(0xBEEF);
    icmp->seq = htons(1);
    icmp->chk = 0;
    icmp->chk = net_checksum(icmp, 8);

    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 64);
    print_col("[NET] ICMP Echo Request transmitted.\n", 0x0E);
    tx_cur = (tx_cur + 1) % 4;
}

void net_send_arp_reply(arp_pkt_t* req) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    arp_pkt_t* arp = (arp_pkt_t*)(buffer + 14);
    for(int i=0; i<6; i++) { eth->dest_mac[i] = req->src_mac[i]; eth->src_mac[i] = my_mac[i]; }
    eth->type = htons(0x0806);
    arp->hw_type = htons(1); arp->proto_type = htons(0x0800);
    arp->hw_len = 6; arp->proto_len = 4; arp->opcode = htons(2);
    for(int i=0; i<6; i++) { arp->src_mac[i] = my_mac[i]; arp->dest_mac[i] = req->src_mac[i]; }
    for(int i=0; i<4; i++) { arp->src_ip[i] = my_ip[i]; arp->dest_ip[i] = req->src_ip[i]; }
    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 64);
    tx_cur = (tx_cur + 1) % 4;
}

/* ========================================================================== */
/* 4. RECEIVE & STATE HANDLING                                                */
/* ========================================================================== */

void net_handle_packet(uint8_t* buf, uint16_t len) {
    eth_hdr_t* eth = (eth_hdr_t*)buf;
    uint16_t type = htons(eth->type);

    if (type == 0x0806) { // ARP
        arp_pkt_t* arp = (arp_pkt_t*)(buf + 14);
        if (htons(arp->opcode) == 1) {
            int is_me = 1;
            for(int i=0; i<4; i++) if(arp->dest_ip[i] != my_ip[i]) is_me = 0;
            if(is_me) net_send_arp_reply(arp);
        }
    } else if (type == 0x0800) { // IP
        ip_hdr_t* ip = (ip_hdr_t*)(buf + 14);
        if (ip->proto == 1) { // ICMP
            print_col("[NET] Ping Reply Received!\n", 0x0A);
        } else if (ip->proto == 6) { // TCP
            tcp_hdr_t* tcp = (tcp_hdr_t*)(buf + 34);
            uint16_t flags = htons(tcp->flags);
            if ((flags & 0x12) == 0x12) { // SYN-ACK
                tcp_ack = htonl(tcp->seq) + 1;
                tcp_seq = htonl(tcp->ack);
                print_col("[TCP] Handshake 2/3. Sending ACK + GET...\n", 0x0A);
                net_send_tcp(ip->src_ip, 80, 0x10, NULL, 0); // Send ACK
                char* get = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
                net_send_tcp(ip->src_ip, 80, 0x18, (uint8_t*)get, 38);
            } else if (len > 54) {
                int p_len = len - 54;
                kmemcpy(browser_buffer + browser_buffer_ptr, buf + 54, p_len);
                browser_buffer_ptr += p_len;
                browser_buffer[browser_buffer_ptr] = '\0';
                browser_ready = 1;
            }
        }
    }
}

void net_poll() {
    if (!net_io_addr) return;
    if (!(inb(net_io_addr + REG_CMD) & 0x01)) {
        uint16_t offset = inw(net_io_addr + REG_CAPR) + 0x10;
        uint16_t header = *(uint16_t*)(rx_buffer + offset);
        uint16_t len = *(uint16_t*)(rx_buffer + offset + 2);
        if (header & 0x01) net_handle_packet(rx_buffer + offset + 4, len - 4);
        outw(net_io_addr + REG_CAPR, offset + len);
    }
}

/* ========================================================================== */
/* 5. INITIALIZATION & COMMANDS                                               */
/* ========================================================================== */

void net_init(uint32_t io_base) {
    net_io_addr = io_base;
    outb(net_io_addr + REG_CONFIG1, 0x00);
    outb(net_io_addr + REG_CMD, 0x10);
    while((inb(net_io_addr + REG_CMD) & 0x10) != 0);
    for(int i=0; i<6; i++) my_mac[i] = inb(net_io_addr + i);
    outl(net_io_addr + REG_RXBUF, (uint32_t)&rx_buffer);
    outw(net_io_addr + REG_IMR, 0x0005);
    outl(net_io_addr + REG_RCR, 0x0F | (1 << 7)); 
    outb(net_io_addr + REG_CMD, 0x0C); 
    print_col("[NET] RTL8139 Online. IP: 10.0.2.15\n", 0x0A);
}

void net_tcp_connect(uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3) {
    uint8_t ip[4] = {i0, i1, i2, i3};
    browser_ready = 0; browser_buffer_ptr = 0;
    net_send_tcp(ip, 80, 0x02, NULL, 0); // Initial SYN
}