#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait() {
    outb(0x80, 0);
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define SERIAL_PORT 0x3F8

static inline int serial_is_transmit_empty() {
    return inb(SERIAL_PORT + 5) & 0x20;
}

static inline void serial_putchar(char c) {
    while (!serial_is_transmit_empty());
    outb(SERIAL_PORT, c);
}

static inline void init_serial() {
    outb(SERIAL_PORT + 1, 0);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 115200 / 9600);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 1, 0);
    outb(SERIAL_PORT + 4, 0x03); /* DTR + RTS for receive */
}

#endif