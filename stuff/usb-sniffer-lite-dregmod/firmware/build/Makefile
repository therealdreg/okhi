##############################################################################
BUILD = build
BIN = okhi

##############################################################################
.PHONY: all directory clean size release

CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

ifeq ($(OS), Windows_NT)
  MKDIR = mkdir
else
  MKDIR = mkdir
endif

CFLAGS += -W -Wall --std=gnu11 -Os
CFLAGS += -g
CFLAGS += -ffreestanding
CFLAGS += -fno-diagnostics-show-caret
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -funsigned-char -funsigned-bitfields
CFLAGS += -mcpu=cortex-m0plus -mthumb
CFLAGS += -MD -MP -MT $(BUILD)/$(*F).o -MF $(BUILD)/$(@F).d

LDFLAGS += -mcpu=cortex-m0plus -mthumb
LDFLAGS += -nostartfiles
LDFLAGS += -Wl,-Map=output.map
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--print-memory-usage
LDFLAGS += -Wl,--script=../linker/rp2040.ld

INCLUDES += \
  -I../include \
  -I..

SRCS += \
  ../main.c \
  ../utils.c \
  ../capture.c \
  ../display.c \
  ../usb.c \
  ../usb_std.c \
  ../usb_cdc.c \
  ../usb_descriptors.c \
  ../startup_rp2040.c

DEFINES += \

CFLAGS += $(INCLUDES) $(DEFINES)

OBJS = $(addprefix $(BUILD)/, $(notdir %/$(subst .c,.o, $(SRCS))))

all: directory $(BUILD)/$(BIN).elf $(BUILD)/$(BIN).hex $(BUILD)/$(BIN).bin $(BUILD)/$(BIN).uf2 release size

$(BUILD)/$(BIN).elf: $(OBJS)
	@echo LD $@
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD)/$(BIN).hex: $(BUILD)/$(BIN).elf
	@echo OBJCOPY $@
	@$(OBJCOPY) -O ihex $^ $@

$(BUILD)/$(BIN).bin: $(BUILD)/$(BIN).elf
	@echo OBJCOPY $@
	@$(OBJCOPY) -O binary $^ $@

$(BUILD)/$(BIN).uf2: $(BUILD)/$(BIN).bin
	@echo BIN2UF2 $@
	@bin2uf2 -i $^ -o $@

#$(BUILD)/$(BIN).uf2: $(BUILD)/$(BIN).elf
#	@echo ELF2UF2 $@
#	elf2uf2.exe -v $^ $@

%.o:
	@echo CC $@
	@$(CC) $(CFLAGS) $(filter %/$(subst .o,.c,$(notdir $@)), $(SRCS)) -c -o $@

release: $(BUILD)/$(BIN).uf2
	@echo release
	#@cp $(BUILD)/$(BIN).uf2 ../../bin/

directory:
	@$(MKDIR) -p $(BUILD)

size: $(BUILD)/$(BIN).elf
	@echo size:
	@$(SIZE) -t $^

clean:
	@echo clean
	@-rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
