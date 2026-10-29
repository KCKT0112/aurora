# Aurora: independent kernel, services, application images and system filesystem.
.DEFAULT_GOAL := all
TARGET ?= x86_64
ifeq ($(wildcard targets/$(TARGET).mk),)
$(error Unsupported TARGET '$(TARGET)')
endif
include targets/$(TARGET).mk
PYTHON ?= python3
HOST_CC ?= cc
TEST_IMAGE ?= 0
RESOLUTION ?= 1440x900
OUT := $(BUILD_ARCH)
FONT_BIN := assets/generated/ui_font.bin
CORE_SRCS := libs/core/elf.c libs/core/display_protocol.c
SOURCES := $(sort $(shell find $(ARCH_DIR) kernel libs/libc libs/ui -name '*.c')) $(CORE_SRCS)
ASM_SOURCES := $(sort $(shell find $(ARCH_DIR) kernel -name '*.S'))
OBJECTS := $(patsubst %.c,$(OUT)/obj/%.o,$(SOURCES)) $(patsubst %.S,$(OUT)/obj/%.o,$(ASM_SOURCES))
FONT_OBJ := $(OUT)/obj/assets/ui_font.o
USER_CFLAGS := -m64 -march=x86-64 -mcmodel=small -mno-red-zone -mno-sse -mno-mmx \
 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
 -fno-asynchronous-unwind-tables $(WARN_FLAGS) $(OPT_FLAGS) -std=gnu11 \
 -Iabi -Ilibs/libc/include -Ilibs/ui/include -Ilibs/user/include -Ilibs/core/include -I$(OUT)/generated -MMD -MP
USER_LDFLAGS := -m64 -nostdlib -static -no-pie -Wl,--build-id=none \
 -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack -Wl,-T,$(ARCH_DIR)/user.ld
PROGRAMS := init authd display input vfsd taskmgr files terminal audiod
ifeq ($(TEST_IMAGE),1)
PROGRAMS += security-test service-test
endif
init_SRCS := servers/init/main.c
authd_SRCS := servers/auth/main.c libs/core/auth.c
display_SRCS := libs/ui/vector.c libs/core/geometry.c servers/display/login.c servers/display/main.c servers/display/desktop.c libs/ui/draw.c libs/ui/font.c
input_SRCS := servers/input/main.c
vfsd_SRCS := servers/vfs/handles.c servers/vfs/main.c libs/core/fs.c libs/core/permissions.c
taskmgr_SRCS := apps/taskmgr/main.c
files_SRCS := apps/files/main.c libs/user/file.c
terminal_SRCS := apps/terminal/main.c libs/core/fs.c libs/user/file.c
audiod_SRCS := servers/audio/main.c libs/core/wav.c libs/user/file.c
security-test_SRCS := apps/security-test/main.c
service-test_SRCS := apps/service-test.c libs/user/file.c
USER_COMMON := libs/user/keymap.c libs/user/start.S libs/user/syscall.c libs/user/model.c libs/libc/string.c
user_objects = $(addprefix $(OUT)/user/obj/,$(patsubst %.S,%.o,$(patsubst %.c,%.o,$(1))))
USER_IMAGES := $(addprefix $(OUT)/user/,$(addsuffix .elf,$(PROGRAMS)))
USER_ALL_SRCS := $(sort $(USER_COMMON) $(foreach app,$(PROGRAMS),$($(app)_SRCS)))
USER_OBJECTS := $(call user_objects,$(USER_ALL_SRCS))
USER_FONT_OBJ := $(OUT)/user/obj/assets/ui_font.o
display_EXTRA := $(USER_FONT_OBJ)

.PHONY: all help kernel user iso fs fonts accounts run run-headless smoke graphics-smoke audio-smoke resize test check clean paste
all: iso
help:
	@echo 'Aurora: kernel user iso run run-headless paste resize test smoke graphics-smoke audio-smoke check fs fonts clean'
	@echo 'make check: host validation + test-image QEMU regression'
kernel: $(KERNEL_ELF)
user: $(USER_IMAGES)
$(OUT)/generated/vector_icons.h: tools/vector_icons.py $(wildcard assets/icons/*.svg)
	$(PYTHON) tools/vector_icons.py --output $@
$(OUT)/obj/libs/ui/vector.o $(OUT)/user/obj/libs/ui/vector.o: $(OUT)/generated/vector_icons.h
$(OUT)/generated/boot_policy.h: config/system.json tools/policy.py
	$(PYTHON) tools/policy.py --config $< --output $@
$(OUT)/obj/kernel/core/process.o: $(OUT)/generated/boot_policy.h
$(KERNEL_ELF): $(OBJECTS) $(FONT_OBJ) $(ARCH_DIR)/boot/linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_LDFLAGS) -Wl,-Map,$(OUT)/aurora.map -o $@ $(OBJECTS) $(FONT_OBJ) -lgcc
$(OUT)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -Ilibs/core/include -I$(OUT)/generated -c $< -o $@
$(OUT)/obj/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_CFLAGS) -c $< -o $@
$(OUT)/user/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(OUT)/user/obj/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(FONT_OBJ) $(USER_FONT_OBJ): $(FONT_BIN)
	@mkdir -p $(dir $@)
	cd assets/generated && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	 --rename-section .data=.rodata,alloc,load,readonly,data,contents ui_font.bin $(abspath $@)
define program
$(OUT)/user/$(1).elf: $(call user_objects,$(USER_COMMON) $($(1)_SRCS)) $($(1)_EXTRA) $(ARCH_DIR)/user.ld
	@mkdir -p $$(dir $$@)
	$(CC) $(USER_LDFLAGS) -o $$@ $(call user_objects,$(USER_COMMON) $($(1)_SRCS)) $($(1)_EXTRA) -lgcc
endef
$(foreach app,$(PROGRAMS),$(eval $(call program,$(app))))
# Packaging always stages a fresh tree; additions/deletions cannot leave stale files.
fs: user
	$(PYTHON) tools/stage_system.py --out $(OUT) --test $(TEST_IMAGE) --resolution $(RESOLUTION)
iso: kernel fs
	$(PYTHON) tools/make_iso.py --out $(OUT) --limine-share '$(LIMINE_SHARE)' --limine '$(LIMINE)'
accounts:
	$(PYTHON) tools/accounts.py
fonts:
	tools/.venv/bin/python tools/fontgen.py
ifeq ($(shell uname -s),Darwin)
$(OUT)/host-window: tools/host_window.m
	@mkdir -p $(dir $@)
	$(HOST_CC) -fobjc-arc -framework Cocoa -framework CoreGraphics $< -o $@
fs: $(OUT)/host-window
endif
run: RESOLUTION=auto
run: iso
	$(PYTHON) tools/run_qemu.py --out $(OUT) --qemu $(QEMU) --fullscreen $(RUN_FULLSCREEN) --audio $(AUDIO_BACKEND) --display '$(QEMU_DISPLAY)'
run-headless: iso
	$(PYTHON) tools/run_qemu.py --out $(OUT) --qemu $(QEMU) --headless --audio $(AUDIO_BACKEND)
resize:
	$(PYTHON) tools/resize.py --out $(OUT) --mode $(RESOLUTION)
paste:
	$(PYTHON) tools/paste.py --out $(OUT)
TEST_OUT ?= out/test
smoke:
	$(MAKE) BUILD_ARCH=$(TEST_OUT) TEST_IMAGE=1 iso
	$(PYTHON) tools/smoke_x64.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
graphics-smoke:
	$(MAKE) BUILD_ARCH=$(TEST_OUT) TEST_IMAGE=1 iso
	$(PYTHON) tools/graphics_smoke.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
audio-smoke:
	$(MAKE) BUILD_ARCH=$(TEST_OUT) TEST_IMAGE=1 iso
	$(PYTHON) tools/audio_smoke.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
test: user $(OUT)/generated/vector_icons.h
	$(PYTHON) tools/check_boundaries.py
	@mkdir -p $(OUT)/tests
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iabi -Ilibs/core/include \
	 tests/core_test.c libs/core/elf.c libs/core/fs.c -o $(OUT)/tests/core_test
	$(OUT)/tests/core_test $(OUT)/user/init.elf
	$(PYTHON) tools/accounts.py --test-output $(OUT)/tests/accounts.bin
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iabi -Ilibs/core/include \
	 tests/auth_test.c libs/core/auth.c libs/core/permissions.c -o $(OUT)/tests/auth_test
	$(OUT)/tests/auth_test $(OUT)/tests/accounts.bin
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ilibs/core/include \
	 tests/geometry_test.c libs/core/geometry.c -o $(OUT)/tests/geometry_test
	$(OUT)/tests/geometry_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ilibs/user/include \
	 tests/test_keymap.c libs/user/keymap.c -o $(OUT)/tests/keymap_test
	$(OUT)/tests/keymap_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ilibs/ui/include -I$(OUT)/generated \
	 tests/render_test.c libs/ui/draw.c libs/ui/font.c libs/ui/vector.c -o $(OUT)/tests/render_test
	$(OUT)/tests/render_test $(FONT_BIN)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ilibs/core/include \
	 tests/display_protocol_test.c libs/core/display_protocol.c -o $(OUT)/tests/display_protocol_test
	$(OUT)/tests/display_protocol_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iabi -Ikernel/include -Iarch/x86_64/include \
	 tests/mailbox_test.c kernel/ipc/ipc.c -o $(OUT)/tests/mailbox_test
	$(OUT)/tests/mailbox_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ilibs/core/include \
	 tests/wav_test.c libs/core/wav.c -o $(OUT)/tests/wav_test
	$(OUT)/tests/wav_test
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' 
check:
	$(MAKE) BUILD_ARCH=$(TEST_OUT) TEST_IMAGE=1 iso test
	$(PYTHON) tools/smoke_x64.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
	$(PYTHON) tools/graphics_smoke.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
	$(PYTHON) tools/audio_smoke.py --iso $(TEST_OUT)/aurora.iso --qemu $(QEMU)
clean:
	rm -rf $(OUT)
-include $(OBJECTS:.o=.d) $(USER_OBJECTS:.o=.d)
