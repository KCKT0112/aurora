/* Aurora v2 - minimal PCI configuration-space access. */
#ifndef AURORA_ARCH_PCI_H
#define AURORA_ARCH_PCI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor;
    uint16_t device_id;
} pci_address_t;

uint32_t pci_config_read32(const pci_address_t *address, uint8_t offset);
uint16_t pci_config_read16(const pci_address_t *address, uint8_t offset);
uint8_t pci_config_read8(const pci_address_t *address, uint8_t offset);
void pci_config_write16(const pci_address_t *address, uint8_t offset,
                        uint16_t value);

bool pci_find_device(uint16_t vendor, uint16_t device_id,
                     pci_address_t *result);
uint64_t pci_bar_address(const pci_address_t *address, uint8_t bar);
void pci_enable_memory(const pci_address_t *address);

#endif /* AURORA_ARCH_PCI_H */
