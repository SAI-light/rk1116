PROJECT := person_pet_detect_test
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(PROJECT)

CROSS_COMPILE ?= arm-rockchip830-linux-uclibcgnueabihf-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

SDK_ROOT ?= $(HOME)/boards/rk1116/sdk/luckfox-pico
ROCKIVA_ROOT ?= $(SDK_ROOT)/media/iva/iva/librockiva/rockiva-rv1106-Linux
ROCKIVA_INCLUDE := $(ROCKIVA_ROOT)/include
ROCKIVA_LIB := $(ROCKIVA_ROOT)/lib

SOURCES := main.c rockiva_detector.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(ROCKIVA_INCLUDE)
CFLAGS += \
	-std=c11 \
	-O2 \
	-Wall \
	-Wextra \
	-Werror \
	-D_POSIX_C_SOURCE=200809L \
	-MMD \
	-MP

LDFLAGS += \
	-L$(ROCKIVA_LIB) \
	-Wl,-rpath-link,$(ROCKIVA_LIB) \
	-Wl,-rpath,/oem/usr/lib

LDLIBS += -lrockiva -lrknnmrt -lstdc++ -lpthread -ldl -lm

.PHONY: all clean check-config print-config strip

all: check-config $(TARGET)

check-config:
	@test -f "$(ROCKIVA_INCLUDE)/rockiva_common.h" || \
		{ echo "Missing RockIVA headers under $(ROCKIVA_INCLUDE)"; exit 1; }
	@test -f "$(ROCKIVA_LIB)/librockiva.so" || \
		{ echo "Missing librockiva.so under $(ROCKIVA_LIB)"; exit 1; }
	@test -f "$(ROCKIVA_LIB)/librknnmrt.so" || \
		{ echo "Missing librknnmrt.so under $(ROCKIVA_LIB)"; exit 1; }

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Built $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

print-config:
	@echo "CC              = $(CC)"
	@echo "SDK_ROOT        = $(SDK_ROOT)"
	@echo "ROCKIVA_INCLUDE = $(ROCKIVA_INCLUDE)"
	@echo "ROCKIVA_LIB     = $(ROCKIVA_LIB)"
	@echo "TARGET          = $(TARGET)"

strip: all
	$(STRIP) $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
