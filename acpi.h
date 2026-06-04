#ifndef ACPI_H
#define ACPI_H
#include <stdint.h>

int acpi_init();
void acpi_poweroff();
void acpi_reboot();
int acpi_is_available();

#endif
