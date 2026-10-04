ROCKET_FLAGS = $(RV_FLAGS) -DMIKOS_ROCKET -DMIKOS_INTERACTIVE
ROCKET_SOURCES = $(filter-out drivers/uart/ns16550a.cpp drivers/net/virtio_mmio.cpp \
  drivers/storage/virtio_block.cpp drivers/storage/root_device.cpp,$(KERNEL_SOURCES)) \
  drivers/uart/rocket.cpp drivers/net/rocket.cpp drivers/storage/rocket.cpp \
  kernel/arch/rocket/htif.cpp
ROCKET_OBJECTS = $(addprefix $(BUILD)/rocket/,$(addsuffix .o,$(basename $(ROCKET_SOURCES)))) \
  $(BUILD)/rocket/hash.o $(BUILD)/rocket/rootfs.o \
  $(TRIBE_EMBEDDED_BUSYBOX_OBJECT) $(TRIBE_EMBEDDED_DROPBEAR_OBJECT) $(KERNEL_SOFTFLOAT_OBJECTS)
$(filter $(BUILD)/rocket/%,$(ROCKET_OBJECTS)): Makefile support/rocket/targets.mk support/toolchain.mk
$(BUILD)/rocket/%.o: %.cpp $(KERNEL_CXX_READY)
	@mkdir -p $(@D)
	$(CXX) $(ROCKET_FLAGS) -c $< -o $@
$(BUILD)/rocket/%.o: %.S
	@mkdir -p $(@D)
	$(CXX) $(ROCKET_FLAGS) -x assembler-with-cpp -c $< -o $@
$(BUILD)/rocket/hash.o: $(KERNEL_CXX_READY)
	@mkdir -p $(@D)
	$(CXX) $(ROCKET_FLAGS) -c $(KERNEL_CXX_HASH) -o $@
$(BUILD)/rocket/rootfs.o: $(ROOTFS_IMAGE)
	@mkdir -p $(@D)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
	  --rename-section .data=.rocket_disk,alloc,load,data,contents $< $@
$(BUILD)/mikos-rocket-rv32.elf: $(ROCKET_OBJECTS) kernel/arch/rocket/linker.ld kernel/arch/riscv32/linker.ld
	$(LD) -m elf32lriscv -nostdlib --gc-sections -T kernel/arch/rocket/linker.ld \
	  -Map $(BUILD)/mikos-rocket-rv32.map -o $@ $(ROCKET_OBJECTS)
.PHONY: rocket-kernel rocket-prepare rocket-test rocket-interactive rocket-all-tests
rocket-kernel: $(BUILD)/mikos-rocket-rv32.elf
rocket-prepare:
	bash tests/rocket/prepare.sh
rocket-interactive:
	bash tests/rocket/rocket_interactive.sh
rocket-test:
	bash tests/rocket/run_kernel.sh
rocket-all-tests:
	bash tests/rocket/run_all.sh
-include $(ROCKET_OBJECTS:.o=.d)
