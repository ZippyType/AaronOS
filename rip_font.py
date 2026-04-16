import sys

def bdf_to_c_array(bdf_file):
    # Initialize a clean array for 256 characters (16 bytes each)
    font_data = [0] * 4096 
    
    with open(bdf_file, 'r') as f:
        current_char = -1
        row = 0
        for line in f:
            if line.startswith("ENCODING"):
                current_char = int(line.split()[1])
            elif line.startswith("BITMAP"):
                row = 0
            # Ensure we only write if char is within our 0-255 limit and we have a valid row
            elif current_char >= 0 and current_char < 256 and row < 16:
                # Check if line is a hex bitmap row
                if len(line.strip()) > 0 and all(c in '0123456789abcdefABCDEF' for c in line.strip()):
                    try:
                        font_data[(current_char * 16) + row] = int(line.strip(), 16)
                        row += 1
                    except ValueError: continue

    with open("font_data.h", "w") as out:
        out.write("uint8_t custom_font[] = {")
        for i, b in enumerate(font_data):
            if i % 16 == 0: out.write("\n  ")
            out.write(f"0x{b:02x}, ")
        out.write("\n};")
    print("Success! Created font_data.h")

if len(sys.argv) > 1:
    bdf_to_c_array(sys.argv[1])
else:
    print("Usage: python3 rip_font.py <file.bdf>")