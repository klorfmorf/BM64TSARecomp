TARGET_NAME  := patches.elf
BUILD_DIR    ?= build
OBJ_DIR      := $(BUILD_DIR)/obj
TARGET       := $(BUILD_DIR)/$(TARGET_NAME)
MAP_FILE     := $(BUILD_DIR)/patches.map
CLANGD_FLAGS := compile_flags.txt

MKDIR_P ?= mkdir -p
RM_R    ?= rm -rf

ifeq ($(origin CC),default)
CC := clang
endif

ifeq ($(origin LD),default)
LD := ld.lld
endif

SRC_DIR := src
INC_DIR := inc

C_SRCS := $(sort $(wildcard $(SRC_DIR)/*.c) $(wildcard *.c))
C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SRCS))
C_DEPS := $(C_OBJS:.o=.d)

S_SRCS := $(sort $(wildcard $(SRC_DIR)/*.s) $(wildcard *.s))
S_OBJS := $(patsubst %.s,$(OBJ_DIR)/%.o,$(S_SRCS))

DEFINES := F3DEX_GBI _LANGUAGE_C MIPS _MIPS_SZLONG=32

INCLUDE_DIRS := \
	$(INC_DIR) \
	. \
	dummy_headers \
	../lib/bm64tsa/include \
	../lib/bm64tsa/include/libultra \
	../lib/bm64tsa/src \
	../lib/rt64/include \
	../lib/N64ModernRuntime/ultramodern/include \
	../lib/N64ModernRuntime/ultramodern/include/ultramodern

LDSCRIPTS := patches.ld syms.ld

OPTFLAGS ?= -O0

CFLAGS := \
	-target mips \
	-mips2 \
	-mabi=32 \
	$(OPTFLAGS) \
	-G0 \
	-mno-abicalls \
	-mno-odd-spreg \
	-mno-check-zero-division \
	-fomit-frame-pointer \
	-ffast-math \
	-fno-unsafe-math-optimizations \
	-fno-builtin-memset \
	-Wall \
	-Wextra \
	-Wno-incompatible-library-redeclaration \
	-Wno-unused-parameter \
	-Wno-unknown-pragmas \
	-Wno-unused-variable \
	-Wno-unused-but-set-variable \
	-Wno-missing-braces \
	-Wno-unsupported-floating-point-opt \
	-Wno-switch \
	$(EXTRA_CFLAGS)

CPPFLAGS := \
	-nostdinc \
	$(addprefix -D,$(DEFINES)) \
	$(addprefix -I,$(INCLUDE_DIRS)) \
	$(EXTRA_CPPFLAGS)

ASFLAGS := \
	-target mips \
	-mips2 \
	-mabi=32 \
	-G0 \
	-mno-abicalls \
	-mno-check-zero-division \
	-x assembler-with-cpp \
	-modd-spreg \

LDFLAGS := \
	-nostdlib \
	$(addprefix -T ,$(LDSCRIPTS)) \
	-Map $(MAP_FILE) \
	--unresolved-symbols=ignore-all \
	--emit-relocs \
	$(EXTRA_LDFLAGS)

all: $(TARGET)

# Linker Rule
$(TARGET): $(C_OBJS) $(S_OBJS) $(LDSCRIPTS) | $(BUILD_DIR)
	$(LD) $(C_OBJS) $(S_OBJS) $(LDFLAGS) -o $@

# Compiler Rules
$(OBJ_DIR)/%.o: %.c Makefile
	$(MKDIR_P) $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/%.o: %.s Makefile
	$(MKDIR_P) $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR_P) $@

$(CLANGD_FLAGS): Makefile
	@printf '%s\n' $(CPPFLAGS) $(CFLAGS) > $@

clangd: $(CLANGD_FLAGS)

clean:
	$(RM_R) $(BUILD_DIR)

-include $(C_DEPS)

.PHONY: all clean clangd
