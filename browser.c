
#include <stdint.h>
#include <stddef.h>

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void clear_screen();
extern void net_tcp_connect(uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3);
extern void net_poll();
extern int browser_ready;
extern char browser_buffer[2048];

void run_browser(char* ip_str) {
    clear_screen();
    print_col("=== AaronBrowse Real-Time Debugger ===\n", 0x0B);
    
    // Parse IP
    int i0 = 93, i1 = 184, i2 = 216, i3 = 34; // Default to example.com if parsing fails
    print("Connecting to IP: "); print(ip_str); print("\n");

    net_tcp_connect(93, 184, 216, 34);

    print("Waiting for Server Response...\n");

    // BLOCKING LOOP: Wait for the network card to finish the handshake
    uint32_t timeout = 0;
    while(!browser_ready && timeout < 5000000) {
        net_poll(); // CRITICAL: Keep polling the card while waiting
        timeout++;
    }

    if (browser_ready) {
        print_col("--- DATA RECEIVED ---\n", 0x07);
        print(browser_buffer);
        print_col("\n--- END OF DATA ---\n", 0x0B);
    } else {
        print_col("Connection timed out. Check QEMU network settings.\n", 0x4F);
    }

    print("\n[Press ESC to return to shell]");
}
