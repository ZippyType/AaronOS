#include <iostream>
#include <string>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>

/**
 * Mounts a disk image to a target directory.
 * @param source Path to the .img file
 * @param target Path to the mount point (must exist)
 * @param fsType Filesystem type (e.g., "ext4", "vfat", "iso9660")
 */
bool mountImage(const std::string& source, const std::string& target, const std::string& fsType) {
    // MS_RDONLY: Mount as read-only
    // For loop mounting via C++, the image usually needs to be attached to 
    // a loop device first (/dev/loopX). However, modern kernels/util-linux 
    // allow direct mounting if the loop module is configured.
    
    unsigned long flags = 0; 
    
    // The loop option is usually handled by the 'mount' command-line tool.
    // In pure C++, we use the mount() system call.
    // Note: To mount a raw .img file, you often need to pass "loop" options
    // or manually setup the loop device using ioctl on /dev/loop-control.
    
    if (mount(source.c_str(), target.c_str(), fsType.c_str(), flags, NULL) == 0) {
        return true;
    } else {
        std::cerr << "Mount failed: " << strerror(errno) << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: sudo ./mount_tool <source_img> <mount_point> <fstype>" << std::endl;
        std::cout << "Example: sudo ./mount_tool my_disk.img /mnt/my_img ext4" << std::endl;
        return 1;
    }

    std::string source = argv[1];
    std::string target = argv[2];
    std::string fsType = argv[3];

    std::cout << "Attempting to mount " << source << " to " << target << "..." << std::endl;

    if (mountImage(source, target, fsType)) {
        std::cout << "Mount successful!" << std::endl;
    } else {
        std::cout << "Mount failed. Ensure you are running as root and the mount point exists." << std::endl;
    }

    return 0;
}