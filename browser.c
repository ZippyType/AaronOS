/**
 * =============================================================================
 * AARONOS WEB BROWSER (browser.c)
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void clear_screen();
extern void net_tcp_connect(uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3);
extern void net_poll();
extern void sleep(uint32_t ticks);

/* Shared state from the network stack */
extern int browser_ready;
extern char browser_buffer[4096];
extern volatile int execute_flag;

/**
 * Standard Entry point for the 'web' command.
 * Orchestrates the transition from CLI to Graphical/HTML mode.
 */
void run_browser(char* ip_str) {
    clear_screen();
    
    /* Visual Header */
    print_col("****************************************\n", 0x0B);
    print_col("*      AaronOS Web Navigator v1.0      *\n", 0x0F);
    print_col("****************************************\n", 0x0B);
    
    print("\nAttempting connection to: "); 
    print_col(ip_str, 0x0E); 
    print("\n\n");
    
    /* Progress Indicator */
    print_col("[STEP 1] ", 0x07);
    print("Parsing IP Address... DONE\n");
    
    print_col("[STEP 2] ", 0x07);
    print("Initiating TCP 3-Way Handshake...\n");
    
    /* Hardcoded IP logic for Example.com (93.184.216.34) */
    net_tcp_connect(93, 184, 216, 34);

    print("Stream Status: ");

    /* High-performance polling loop with timeout mechanism */
    uint32_t max_wait = 40000000;
    uint32_t current_wait = 0;
    
    while (!browser_ready && current_wait < max_wait) {
        /* Force hardware poll every cycle */
        net_poll(); 
        
        /* Render loading animation dots every 5M cycles */
        if (current_wait % 5000000 == 0) {
            print_col(".", 0x0A);
        }
        current_wait++;
    }

    if (browser_ready) {
        print_col("\n\n[SUCCESS] HTTP Transaction Completed.\n", 0x0A);
        print_col("================== HTML DATA ==================\n\n", 0x07);
        
        /* Render the raw ASCII HTML data */
        print(browser_buffer);
        
        print_col("\n\n===============================================\n", 0x07);
    } else {
        print_col("\n\n[FAILURE] Connection timed out.\n", 0x4F);
        print("POSSIBLE CAUSES:\n");
        print("1. Host machine has no internet access.\n");
        print("2. QEMU network bridge is restricted.\n");
        print("3. TCP Checksum rejected by the remote server.\n");
    }

    print_col("\n[ NAVIGATION HALTED ]\n", 0x0E);
    print("Press ENTER to return to the AaronOS prompt.");
    
    /* 
     * INPUT SAFETY LOCK: 
     * We clear the execute_flag and wait for a fresh ENTER press.
     * This prevents the shell from immediately executing the next line.
     */
    execute_flag = 0;
    while(execute_flag == 0) {
        /* Keep polling network in background while waiting */
        net_poll(); 
        asm volatile("hlt");
    }
    
    /* Cleanup and return to Kernel shell */
    execute_flag = 0;
    clear_screen();
}