# Aurora v2 - x86_64 target configuration.

ARCH              := x86_64
ARCH_DIR          := arch/x86_64
BUILD_ARCH        := out/x86_64
KERNEL_ELF        := $(BUILD_ARCH)/aurora.elf
ISO               := $(BUILD_ARCH)/aurora.iso

CROSS             ?= x86_64-elf-
CC                := $(CROSS)gcc
OBJCOPY           := $(CROSS)objcopy

QEMU              ?= qemu-system-x86_64
AUDIO_BACKEND     ?= $(if $(filter Darwin,$(shell uname -s)),coreaudio,none)
QEMU_MACHINE      := -machine q35,pcspk-audiodev=audio -m 512M -cpu max
QEMU_AUDIO        := -audiodev $(AUDIO_BACKEND),id=audio
RUN_FULLSCREEN    ?= on
QEMU_DISPLAY      ?=
# tools/run_qemu.py selects native-pixel Cocoa/GTK; override only intentionally.
QEMU_DEVICES      := -device virtio-tablet-pci -device AC97,audiodev=audio $(QEMU_AUDIO)
QEMU_FLAGS        := $(QEMU_MACHINE) -cdrom $(ISO) -boot d -vga std \
                     -serial stdio -no-reboot $(QEMU_DEVICES) \
                     $(QEMU_DISPLAY)

# Limine host tooling/assets. Homebrew layout is the development default;
# override LIMINE_SHARE or LIMINE_BIN for another installation.
LIMINE            ?= limine
LIMINE_SHARE      ?= $(shell brew --prefix limine 2>/dev/null)/share/limine

ARCH_CFLAGS := -m64 -march=x86-64 -mcmodel=kernel -mno-red-zone \
               -mno-sse -mno-mmx -ffreestanding -fno-builtin \
               -fno-stack-protector -fno-pic -fno-pie \
               -fno-asynchronous-unwind-tables
WARN_FLAGS  := -Wall -Wextra -Wshadow -Wstrict-prototypes -Werror
OPT_FLAGS   := -O2 -g
KERNEL_CFLAGS := $(ARCH_CFLAGS) $(WARN_FLAGS) $(OPT_FLAGS) -std=gnu11 \
                 -Iabi -I$(ARCH_DIR)/include -I$(ARCH_DIR)/boot \
                 -Ilibs/ui/include \
                 -Ikernel/include -Ilibs/libc/include -MMD -MP
KERNEL_LDFLAGS := -m64 -nostdlib -static -no-pie -Wl,--build-id=none \
                  -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack \
                  -Wl,--gc-sections -Wl,-T,$(ARCH_DIR)/boot/linker.ld
