/* Aurora v2 - PCI configuration-space access over the q35 CAM ports. */
#include <arch/io.h>
#include <arch/pci.h>
#include <stddef.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu
#define PCI_COMMAND 0x04u
#define PCI_COMMAND_MEMORY 0x0002u
#define PCI_COMMAND_MASTER 0x0004u
#define PCI_HEADER_TYPE 0x0Eu
#define PCI_HEADER_MULTIFUNCTION 0x80u

static uint32_t config_address(const pci_address_t *address, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)address->bus << 16) |
           ((uint32_t)address->device << 11) |
           ((uint32_t)address->function << 8) | (offset & 0xFCu);
}

uint32_t pci_config_read32(const pci_address_t *address, uint8_t offset)
{
    if (address == NULL) {
        return 0xFFFFFFFFu;
    }
    outl(PCI_CONFIG_ADDRESS, config_address(address, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(const pci_address_t *address, uint8_t offset)
{
    uint32_t value = pci_config_read32(address, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

uint8_t pci_config_read8(const pci_address_t *address, uint8_t offset)
{
    uint32_t value = pci_config_read32(address, offset);
    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

void pci_config_write16(const pci_address_t *address, uint8_t offset,
                        uint16_t value)
{
    if (address == NULL) {
        return;
    }
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t current = pci_config_read32(address, offset);
    current &= ~(0xFFFFu << shift);
    current |= (uint32_t)value << shift;
    outl(PCI_CONFIG_ADDRESS, config_address(address, offset));
    outl(PCI_CONFIG_DATA, current);
}

static bool probe_function(uint8_t bus, uint8_t device, uint8_t function,
                           uint16_t vendor, uint16_t device_id,
                           pci_address_t *result)
{
    pci_address_t address = {
        .bus = bus,
        .device = device,
        .function = function,
        .vendor = 0,
        .device_id = 0,
    };
    address.vendor = pci_config_read16(&address, 0x00);
    if (address.vendor == 0xFFFFu || address.vendor != vendor) {
        return false;
    }
    address.device_id = pci_config_read16(&address, 0x02);
    if (address.device_id != device_id) {
        return false;
    }
    if (result != NULL) {
        *result = address;
    }
    return true;
}

bool pci_find_device(uint16_t vendor, uint16_t device_id,
                     pci_address_t *result)
{
    /* q35 places all built-in and simple -device PCI endpoints on bus 0. */
    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t function = 0; function < 8; function++) {
            pci_address_t address = {
                .bus = 0,
                .device = device,
                .function = function,
                .vendor = 0,
                .device_id = 0,
            };
            uint16_t found = pci_config_read16(&address, 0x00);
            if (found == 0xFFFFu) {
                if (function == 0) {
                    break;
                }
                continue;
            }
            if (probe_function(0, device, function, vendor, device_id,
                               result)) {
                return true;
            }
            if (function == 0 &&
                (pci_config_read8(&address, PCI_HEADER_TYPE) &
                 PCI_HEADER_MULTIFUNCTION) == 0) {
                break;
            }
        }
    }
    return false;
}

uint64_t pci_bar_address(const pci_address_t *address, uint8_t bar)
{
    if (address == NULL || bar > 5) {
        return 0;
    }
    uint8_t offset = (uint8_t)(0x10u + bar * 4u);
    uint32_t low = pci_config_read32(address, offset);
    if ((low & 1u) != 0) {
        return 0; /* I/O BAR */
    }
    uint64_t base = low & ~0xFull;
    if ((low & 0x6u) == 0x4u && bar < 5) {
        uint32_t high = pci_config_read32(address, offset + 4);
        base |= (uint64_t)high << 32;
    }
    return base;
}

void pci_enable_memory(const pci_address_t *address)
{
    uint16_t command = pci_config_read16(address, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(address, PCI_COMMAND, command);
}
