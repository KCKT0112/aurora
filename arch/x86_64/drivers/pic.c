/* Aurora v2 - legacy 8259A PIC. */
#include <arch/io.h>
#include <arch/pic.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

void pic_remap(uint8_t master_offset, uint8_t slave_offset)
{
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    pic_mask_all();
}

void pic_mask_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    io_wait();
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
        uint8_t master = inb(PIC1_DATA);
        outb(PIC1_DATA, (uint8_t)(master & ~(1u << 2)));
    }
    uint8_t value = inb(port);
    value &= (uint8_t)~(1u << irq);
    outb(port, value);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}
