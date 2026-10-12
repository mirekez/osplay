# RV64 Rocket keeps the existing RV32 kernel, userspace and objects separate.
RISCV64_PREFIX ?= $(if $(findstring riscv64,$(RISCV_PREFIX)),$(RISCV_PREFIX),$(HOME)/riscv64/bin/riscv64-unknown-linux-gnu-)
RISCV64_SYSROOT ?= $(shell $(RISCV64_PREFIX)gcc -print-sysroot 2>/dev/null)
RISCV64_C_INCLUDE = $(if $(strip $(RISCV64_SYSROOT)),$(firstword $(wildcard $(RISCV64_SYSROOT)/usr/include $(RISCV64_SYSROOT)/include)))
ROCKET64_HEADERS = $(KERNEL_CXX_HEADERS) \
  $(if $(RISCV64_C_INCLUDE),--sysroot=$(RISCV64_SYSROOT) -isystem $(RISCV64_C_INCLUDE),$(error RV64 C headers not found; set RISCV64_PREFIX and optionally RISCV64_SYSROOT))
ROCKET64_USER_BUILD := $(BUILD)/tests/busybox-rv64
ROCKET64_FLAGS = --target=riscv64-unknown-elf -march=rv64imafd_zicsr_zifencei -mabi=lp64d \
  $(RV_COMMON_FLAGS) $(ROCKET64_HEADERS) \
  -DOSPLAY_ROCKET -DOSPLAY_INTERACTIVE -DOSPLAY_RV64 $(ROCKET64_EXTRA_FLAGS)
ROCKET64_SOURCES = $(filter-out kernel/arch/riscv32/entry.S kernel/arch/riscv32/trap.S drivers/net/rocket.cpp,$(ROCKET_SOURCES)) \
  kernel/arch/riscv64/entry.S kernel/arch/riscv64/trap.S drivers/net/rocket64.cpp \
  drivers/storage/rocket_ramdisk.cpp
ROCKET64_OBJECTS = $(addprefix $(BUILD)/rocket64/,$(addsuffix .o,$(basename $(ROCKET64_SOURCES)))) \
  $(BUILD)/rocket64/hash.o $(BUILD)/rocket64/rootfs.o \
  $(BUILD)/rocket64/busybox.o $(BUILD)/rocket64/dropbear.o
$(ROCKET64_OBJECTS): $(BUILD)/rocket64/compiler.flags
.PHONY: rocket64-force
rocket64-force:
$(BUILD)/rocket64/compiler.flags: rocket64-force
	@mkdir -p $(@D)
	@printf '%s\n' '$(CXX) $(ROCKET64_FLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/rocket64/%.o: %.cpp $(KERNEL_CXX_READY) support/rocket/rv64.mk
	@mkdir -p $(@D)
	$(CXX) $(ROCKET64_FLAGS) -c $< -o $@
$(BUILD)/rocket64/%.o: %.S support/rocket/rv64.mk
	@mkdir -p $(@D)
	$(CXX) $(ROCKET64_FLAGS) -x assembler-with-cpp -c $< -o $@
$(BUILD)/rocket64/hash.o: $(KERNEL_CXX_READY) support/rocket/rv64.mk
	@mkdir -p $(@D)
	$(CXX) $(ROCKET64_FLAGS) -c $(KERNEL_CXX_HASH) -o $@
$(BUILD)/rocket64/rootfs.o: $(ROCKET64_USER_BUILD)/rootfs.ext4
	@mkdir -p $(@D)
	$(RISCV64_PREFIX)objcopy -I binary -O elf64-littleriscv -B riscv \
	  --rename-section .data=.rocket_disk,alloc,load,data,contents $< $@
$(BUILD)/rocket64/busybox.o: $(ROCKET64_USER_BUILD)/rootfs.ext4
	@mkdir -p $(@D)
	$(RISCV64_PREFIX)objcopy -I binary -O elf64-littleriscv -B riscv \
	  --rename-section .data=.user_busybox,alloc,load,readonly,data,contents $(ROCKET64_USER_BUILD)/busybox/busybox $@
$(BUILD)/rocket64/dropbear.o: $(ROCKET64_USER_BUILD)/rootfs.ext4
	@mkdir -p $(@D)
	$(RISCV64_PREFIX)objcopy -I binary -O elf64-littleriscv -B riscv \
	  --rename-section .data=.user_dropbear,alloc,load,readonly,data,contents $(ROCKET64_USER_BUILD)/dropbear-source/dropbear $@
$(BUILD)/osplay-rocket-rv64.elf: $(ROCKET64_OBJECTS) kernel/arch/riscv64/linker.ld
	$(RISCV64_PREFIX)ld -m elf64lriscv -nostdlib --no-relax --gc-sections -T kernel/arch/riscv64/linker.ld \
	  -Map $(BUILD)/osplay-rocket-rv64.map -o $@ $(ROCKET64_OBJECTS) $$($(RISCV64_PREFIX)gcc -print-libgcc-file-name)
	$(LLVM_READELF) -h -l $@ > $(BUILD)/osplay-rocket-rv64.headers.txt
.PHONY: rocket64-kernel
rocket64-kernel: $(BUILD)/osplay-rocket-rv64.elf
-include $(ROCKET64_OBJECTS:.o=.d)
