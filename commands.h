#ifndef COMMANDS_H
#define COMMANDS_H

#define CMD(name, desc) { name, desc }

typedef struct {
    const char* name;
    const char* description;
} command_t;

command_t commands[] = {
    CMD("help",     "Show all commands"),
    CMD("gui",      "Open TUI Explorer"),
    CMD("ver",      "Show system version"),
    CMD("reboot",   "Warm restart"),
    CMD("shutdown", "Power off"),
    CMD("cls",      "Clear terminal"),
    CMD("dmesg",    "Show boot log"),
    CMD("install",  "Install AaronOS to hard disk"),
    CMD("edit",     "Edit a file (edit filename)"),
    CMD("panic",    "Test kernel crash"),
    CMD("cpu",      "Show CPU vendor"),
    CMD("credits",  "Show build info"),
    CMD("stats",    "Show health & uptime"),
    CMD("time",     "Show clock & date"),
    CMD("tz",       "Set timezone (tz amsterdam|london|newyork|tokyo)"),
    CMD("dir",      "List disk files"),
    CMD("ls",       "List disk files"),
    CMD("cat",      "Read file (cat filename)"),
    CMD("write",    "Open text editor (write filename)"),
    CMD("touch",    "Create file (touch filename)"),
    CMD("rm",       "Delete file (rm filename)"),
    CMD("rename",   "Rename file (rename old new)"),
    CMD("cp",       "Copy file (cp src dst)"),
    CMD("mv",       "Move/rename file (mv src dst)"),
    CMD("mkdir",    "Create directory (mkdir name)"),
    CMD("rmdir",    "Remove directory (rmdir name)"),
    CMD("format",   "Format disk"),
    CMD("echo",     "Print text (echo text)"),
    CMD("rand",     "Random number"),
    CMD("color",    "Set color (color hex)"),
    CMD("calc",     "Math (calc 5+5 or calc sin 90)"),
    CMD("beep",     "Play tone (beep 440)"),
    CMD("music",    "Play melody"),
    CMD("siren",    "Play siren"),
    CMD("matrix",   "Enter the matrix"),
    CMD("netstat",  "Network status"),
    CMD("web",      "Web browser (web ip)"),
    CMD("ping",     "Ping host (ping ip)"),
    CMD("clear",    "Clear terminal"),
    CMD("serial",   "Toggle serial output (serial on|off)"),
    CMD("set",      "List shell variables"),
    CMD("export",   "Set shell variable (export NAME=VALUE)"),
    CMD("grep",     "Search piped input (grep pattern)"),
    CMD("head",     "Show first lines of piped input (head [n])"),
    CMD("wc",       "Count lines/words/chars of piped input"),
    CMD("sort",     "Sort lines of piped input"),
    CMD("poweroff", "Power off system (ACPI)"),
    CMD("sb16",     "Sound Blaster 16 test (sb16 test)"),
    CMD("attrib",   "View/set file attributes (attrib [+-RHS] file)"),
    CMD("play",     "Play WAV file (play file.wav)"),
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(command_t))

#endif