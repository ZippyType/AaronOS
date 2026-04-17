/**
 * =============================================================================
 * AARONOS NETWORK SUBSYSTEM (net.c)
 * =============================================================================
 * VERSION: 1.2.7 (Stable Monolithic Network Stack)
 * HARDWARE: Realtek RTL8139 Fast Ethernet (PCI)
 * 
 * PROTOCOL SUPPORT:
 * - Layer 2: Ethernet II (MAC Addressing)
 * - Layer 2.5: ARP (Address Resolution Protocol) with auto-reply
 * - Layer 3: IPv4 (Internet Protocol version 4)
 * - Layer 4: ICMP (Control Message Protocol - PING)
 * - Layer 4: TCP (Transmission Control Protocol - 3-Way Handshake)
 * 
 * DESCRIPTION:
 * This module provides bare-metal hardware access to the RTL8139.
 * It manages memory-aligned DMA buffers for transmission and reception.
 * All checksum calculations are handled via byte-wise accumulation to
 * ensure compatibility with packed structures and prevent CPU alignment faults.
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include "io.h"

/* --- 1. RTL8139 REGISTER DEFINITIONS --- */
#define REG_MAC          0x00    /* MAC Address (6 bytes) */
#define REG_MAR          0x08    /* Multicast Mask (8 bytes) */
#define REG_TXSTATUS0    0x10    /* Transmit Status (4 bytes) */
#define REG_TXADDR0      0x20    /* Transmit Start Address (4 bytes) */
#define REG_RXBUF        0x30    /* Receive Buffer Start Address */
#define REG_CMD          0x37    /* Command Register */
#define REG_CAPR         0x38    /* Current Address of Packet Read */
#define REG_IMR          0x3C    /* Interrupt Mask Register */
#define REG_ISR          0x3E    /* Interrupt Status Register */
#define REG_RCR          0x44    /* Receive Configuration Register */
#define REG_CONFIG1      0x52    /* Configuration Register 1 */

/* --- 2. SHARED KERNEL REFERENCES --- */
extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void itoa(int num, char* str, int base);
extern void kmemcpy(void* dest, const void* src, size_t len);
extern void sleep(uint32_t ticks);

/* --- 3. HARDWARE BUFFER ALLOCATION --- */
/* The RTL8139 uses DMA. These buffers MUST be aligned on a 4-byte boundary. */
uint8_t rx_buffer[8192 + 16 + 1500] __attribute__((aligned(4)));
uint8_t tx_buffer[4][1536] __attribute__((aligned(4)));
uint8_t tx_cur = 0;

/* --- 4. NETWORK STATE VARIABLES --- */
uint32_t net_io_addr = 0;
uint8_t my_mac[6];
uint8_t my_ip[4] = {10, 0, 2, 15};             
uint8_t router_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02}; 

/* TCP Session State Machine */
uint32_t tcp_seq = 0x12345678;
uint32_t tcp_ack = 0;
int tcp_state = 0;                             /* 0:Closed, 1:Handshaking, 2:Open */
char browser_buffer[4096];                     /* Expanded buffer for HTML data */
int browser_buffer_ptr = 0;
int browser_ready = 0;

/* --- 5. DATA STRUCTURES (PACKED) --- */

typedef struct {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_addr_len;
    uint8_t  proto_addr_len;
    uint16_t opcode;
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];
    uint8_t  dest_mac[6];
    uint8_t  dest_ip[4];
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    uint8_t  v_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dest_ip[4];
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_hdr_t;

/* ========================================================================== */
/* 6. PROTOCOL UTILITIES                                                      */
/* ========================================================================== */

uint16_t htons(uint16_t val) {
    return (val << 8) | (val >> 8);
}

uint32_t htonl(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val & 0xFF0000) >> 8) | ((val >> 24) & 0xFF);
}

/**
 * Standard Internet Checksum (RFC 1071).
 * Uses byte-accumulation to avoid alignment warnings on packed structs.
 */
uint16_t calculate_checksum(const void* vdata, size_t length) {
    const uint8_t* data = (const uint8_t*)vdata;
    uint32_t sum = 0;
    
    for (size_t i = 0; i < (length & ~1); i += 2) {
        uint16_t word = (uint16_t)data[i] | ((uint16_t)data[i+1] << 8);
        sum += word;
    }
    
    if (length & 1) {
        sum += (uint16_t)data[length - 1];
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

/**
 * TCP Checksum (Requires Pseudo-Header).
 * Manually assembles the pseudo-header bytes to ensure zero-warning compilation.
 */
uint16_t calculate_tcp_checksum(const ip_hdr_t* ip, const tcp_hdr_t* tcp, const uint8_t* data, int data_len) {
    uint32_t sum = 0;
    const uint8_t* ip_raw = (const uint8_t*)ip;
    const uint8_t* tcp_raw = (const uint8_t*)tcp;

    /* 1. Sum Pseudo-Header (Source IP, Destination IP, Zero, Protocol 6, TCP Length) */
    sum += ((uint16_t)ip_raw[12] | (uint16_t)ip_raw[13] << 8);
    sum += ((uint16_t)ip_raw[14] | (uint16_t)ip_raw[15] << 8);
    sum += ((uint16_t)ip_raw[16] | (uint16_t)ip_raw[17] << 8);
    sum += ((uint16_t)ip_raw[18] | (uint16_t)ip_raw[19] << 8);
    sum += htons(6); 
    sum += htons(20 + data_len);

    /* 2. Sum TCP Header (Fixed 20 Bytes) */
    for (int i = 0; i < 20; i += 2) {
        sum += ((uint16_t)tcp_raw[i] | (uint16_t)tcp_raw[i+1] << 8);
    }

    /* 3. Sum TCP Payload */
    for (int i = 0; i < (data_len & ~1); i += 2) {
        sum += ((uint16_t)data[i] | (uint16_t)data[i+1] << 8);
    }
    if (data_len & 1) {
        sum += (uint16_t)data[data_len - 1];
    }

    /* 4. Finalize One's Complement */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

/* ========================================================================== */
/* 7. TRANSMISSION LOGIC                                                      */
/* ========================================================================== */

void net_send_tcp(uint8_t* dest_ip, uint16_t dest_port, uint16_t flags, uint8_t* data, uint16_t data_len) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    ip_hdr_t* ip = (ip_hdr_t*)(buffer + 14);
    tcp_hdr_t* tcp = (tcp_hdr_t*)(buffer + 34);

    /* Build Ethernet Frame */
    kmemcpy(eth->dest, router_mac, 6); 
    kmemcpy(eth->src, my_mac, 6);
    eth->type = htons(0x0800);

    /* Build IPv4 Header */
    ip->v_ihl = 0x45; 
    ip->tos = 0; 
    ip->total_len = htons(40 + data_len); 
    ip->id = htons(1234);
    ip->flags_offset = 0;
    ip->ttl = 64; 
    ip->protocol = 6; /* TCP */
    kmemcpy(ip->src_ip, my_ip, 4); 
    kmemcpy(ip->dest_ip, dest_ip, 4);
    ip->checksum = 0; 
    ip->checksum = calculate_checksum(ip, 20);

    /* Build TCP Header */
    tcp->src_port = htons(12345); 
    tcp->dest_port = htons(dest_port);
    tcp->seq_num = htonl(tcp_seq); 
    tcp->ack_num = htonl(tcp_ack);
    tcp->flags = htons(flags | 0x5000); /* Header size 5 (20 bytes) */
    tcp->window_size = htons(8192);
    tcp->checksum = 0; 
    tcp->urgent_ptr = 0;
    
    if(data_len > 0) {
        kmemcpy(buffer + 54, data, data_len);
    }
    
    tcp->checksum = calculate_tcp_checksum(ip, tcp, buffer + 54, data_len);

    /* Dispatch to hardware */
    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 14 + 40 + data_len);
    
    /* Rotate hardware TX buffers (0-3) */
    tx_cur = (tx_cur + 1) % 4;
}

void net_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    ip_hdr_t* ip = (ip_hdr_t*)(buffer + 14);
    icmp_hdr_t* icmp = (icmp_hdr_t*)(buffer + 34);
    uint8_t dip[4] = {ip0, ip1, ip2, ip3};

    kmemcpy(eth->dest, router_mac, 6); 
    kmemcpy(eth->src, my_mac, 6); 
    eth->type = htons(0x0800);
    
    ip->v_ihl = 0x45; 
    ip->total_len = htons(28); 
    ip->ttl = 64; 
    ip->protocol = 1; /* ICMP */
    kmemcpy(ip->src_ip, my_ip, 4); 
    kmemcpy(ip->dest_ip, dip, 4);
    ip->checksum = 0; 
    ip->checksum = calculate_checksum(ip, 20);
    
    icmp->type = 8; /* Echo Request */
    icmp->code = 0; 
    icmp->checksum = 0; 
    icmp->id = htons(0xBEEF); 
    icmp->seq = htons(1);
    icmp->checksum = calculate_checksum(icmp, 8);

    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 64);
    
    print_col("[NET] ICMP Diagnostic Frame Dispatched.\n", 0x0E);
    tx_cur = (tx_cur + 1) % 4;
}

void net_send_arp_reply(arp_pkt_t* req) {
    uint8_t* buffer = tx_buffer[tx_cur];
    eth_hdr_t* eth = (eth_hdr_t*)buffer;
    arp_pkt_t* arp = (arp_pkt_t*)(buffer + 14);
    
    kmemcpy(eth->dest, req->src_mac, 6); 
    kmemcpy(eth->src, my_mac, 6); 
    eth->type = htons(0x0806);
    
    arp->hw_type = htons(1); 
    arp->proto_type = htons(0x0800); 
    arp->hw_addr_len = 6; 
    arp->proto_addr_len = 4;
    arp->opcode = htons(2); /* Reply */
    
    kmemcpy(arp->src_mac, my_mac, 6); 
    kmemcpy(arp->dest_mac, req->src_mac, 6);
    kmemcpy(arp->src_ip, my_ip, 4); 
    kmemcpy(arp->dest_ip, req->src_ip, 4);
    
    outl(net_io_addr + REG_TXADDR0 + (tx_cur * 4), (uint32_t)buffer);
    outl(net_io_addr + REG_TXSTATUS0 + (tx_cur * 4), 60);
    tx_cur = (tx_cur + 1) % 4;
}

/* ========================================================================== */
/* 8. RECEIVE ENGINE & PACKET PARSING                                         */
/* ========================================================================== */

void net_handle_packet(uint8_t* buf, uint16_t len) {
    eth_hdr_t* eth = (eth_hdr_t*)buf;
    uint16_t eth_type = htons(eth->type);

    /* --- ARP Handler --- */
    if (eth_type == 0x0806) {
        arp_pkt_t* arp = (arp_pkt_t*)(buf + 14);
        if (htons(arp->opcode) == 1) { /* Request */
            print("[NET] Caught ARP Broadcast. Identity Verification Sent.\n");
            net_send_arp_reply(arp);
        }
    } 
    /* --- IPv4 Handler --- */
    else if (eth_type == 0x0800) {
        ip_hdr_t* ip = (ip_hdr_t*)(buf + 14);
        
        /* ICMP Parser */
        if (ip->protocol == 1) {
            print_col("[NET] ICMP Stack: Response Received from Host.\n", 0x0A);
        } 
        /* TCP Parser */
        else if (ip->protocol == 6) {
            tcp_hdr_t* tcp = (tcp_hdr_t*)(buf + 34);
            uint16_t flags = htons(tcp->flags);
            
            /* SYN-ACK Handshake Response */
            if ((flags & 0x12) == 0x12) { 
                tcp_ack = htonl(tcp->seq_num) + 1; 
                tcp_seq = htonl(tcp->ack_num);
                print_col("[TCP] Handshake Phase 2/3 complete. Established.\n", 0x0A);
                
                /* Final Handshake Step: ACK */
                net_send_tcp(ip->src_ip, 80, 0x10, NULL, 0); 
                
                /* Immediate HTTP Request */
                char* get_req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
                net_send_tcp(ip->src_ip, 80, 0x18, (uint8_t*)get_req, 58);
                print_col("[HTTP] Request Header Transmitted.\n", 0x0B);
                
            } 
            /* Payload Data Processing */
            else if (len > 54) {
                int payload_len = len - 54;
                if(browser_buffer_ptr + payload_len < 4000) {
                    kmemcpy(browser_buffer + browser_buffer_ptr, buf + 54, payload_len);
                    browser_buffer_ptr += payload_len;
                    browser_ready = 1;
                }
            }
        }
    }
}

/**
 * High-frequency polling loop called by the kernel main cycle.
 * Ensures data is cleared from the NIC hardware buffer immediately.
 */
void net_poll() {
    if (!net_io_addr) return;
    
    /* Check REG_CMD: If bit 0 is low, buffer has data */
    if (!(inb(net_io_addr + REG_CMD) & 0x01)) {
        uint32_t read_ptr = (uint32_t)inw(net_io_addr + REG_CAPR) + 0x10;
        uint16_t status = *(uint16_t*)(rx_buffer + read_ptr);
        uint16_t length = *(uint16_t*)(rx_buffer + read_ptr + 2);
        
        if (status & 0x01) { /* Receive OK */
            net_handle_packet(rx_buffer + read_ptr + 4, length - 4);
        }
        
        /* Update CAPR with 4-byte alignment padding */
        uint32_t updated_capr = (read_ptr + length + 4 + 3) & ~3;
        outw(net_io_addr + REG_CAPR, (uint16_t)(updated_capr - 0x10));
    }
}

/* ========================================================================== */
/* 9. INITIALIZATION & PUBLIC API                                             */
/* ========================================================================== */

void net_init(uint32_t io_base) {
    net_io_addr = io_base;
    
    /* 1. Wake up hardware */
    outb(net_io_addr + REG_CONFIG1, 0x00);
    
    /* 2. Soft Reset */
    outb(net_io_addr + REG_CMD, 0x10); 
    while((inb(net_io_addr + REG_CMD) & 0x10) != 0);
    
    /* 3. Harvest Hardware MAC */
    for(int i=0; i<6; i++) {
        my_mac[i] = inb(net_io_addr + i);
    }
    
    /* 4. Register DMA Receive Area */
    outl(net_io_addr + REG_RXBUF, (uint32_t)&rx_buffer);
    
    /* 5. Mask all except ROK and TOK interrupts */
    outw(net_io_addr + REG_IMR, 0x0005);
    
    /* 6. Configure Receive settings (Accept Broadcast + Multicast + Physical) */
    outl(net_io_addr + REG_RCR, 0x0F | (1 << 7)); 
    
    /* 7. Final Enable */
    outb(net_io_addr + REG_CMD, 0x0C); 
    
    print_col("[HAL] Network Interface Controller (RTL8139) Linked.\n", 0x0A);
}

void net_tcp_connect(uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3) {
    uint8_t target_ip[4] = {i0, i1, i2, i3};
    
    /* Initialize Browser context */
    browser_ready = 0; 
    browser_buffer_ptr = 0;
    tcp_seq = 0x55AA1234; 
    tcp_ack = 0;
    tcp_state = 1;
    
    print_col("[TCP] Starting sequence 0x55AA1234... Sending SYN.\n", 0x0B);
    net_send_tcp(target_ip, 80, 0x02, NULL, 0); 
}