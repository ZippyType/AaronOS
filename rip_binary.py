import sys

def psf_to_c_array(psf_file):
    with open(psf_file, 'rb') as f:
        f.read(4) # Skip header
        data = f.read(4096) # Read ONLY the 4096 bytes of font data
    
    with open("font_data.h", "w") as out:
        out.write("#ifndef FONT_DATA_H\n#define FONT_DATA_H\n#include <stdint.h>\n\n")
        out.write("uint8_t custom_font[] = {")
        for i, b in enumerate(data):
            if i % 16 == 0: out.write("\n  ")
            out.write(f"0x{b:02x}, ")
        out.write("\n};\n\n#endif")

psf_to_c_array(sys.argv[1])