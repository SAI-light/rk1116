# Independent live PERSON detection test for Luckfox Pico Max / RV1106

PROJECT := live_person_detect_test
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(PROJECT)

CROSS_COMPILE ?= arm-rockchip830-linux-uclibcgnueabihf-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

# This tool is intended to live at:
# mini_rtsp_server_project/tools/live_person_detect_test
PROJECT_ROOT := $(abspath ../..)
SDK_ROOT ?= $(HOME)/boards/rk1116/sdk/luckfox-pico

ROCKIVA_ROOT ?= \
	$(SDK_ROOT)/media/iva/iva/librockiva/rockiva-rv1106-Linux
ROCKIVA_INCLUDE := $(ROCKIVA_ROOT)/include
ROCKIVA_LIB := $(ROCKIVA_ROOT)/lib

# Auto-detect the RV1106 RKAIQ release directory in the Luckfox SDK.
RKAIQ_ROOT ?= $(firstword \
	$(wildcard \
		$(SDK_ROOT)/media/isp/release_camera_engine_rkaiq_rv1106*))
RKAIQ_INCLUDE_ROOT := $(RKAIQ_ROOT)/include/rkaiq
RKAIQ_LIB := $(RKAIQ_ROOT)/lib

OBJECTS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/live_person_detector.o \
	$(BUILD_DIR)/v4l2_capture.o \
	$(BUILD_DIR)/isp_control.o \
	$(BUILD_DIR)/log.o

DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += \
	-I. \
	-I$(PROJECT_ROOT)/capture \
	-I$(PROJECT_ROOT)/isp \
	-I$(PROJECT_ROOT)/include \
	-I$(ROCKIVA_INCLUDE) \
	-I$(RKAIQ_INCLUDE_ROOT) \
	-I$(RKAIQ_INCLUDE_ROOT)/uAPI2 \
	-I$(RKAIQ_INCLUDE_ROOT)/common \
	-I$(RKAIQ_INCLUDE_ROOT)/xcore \
	-I$(RKAIQ_INCLUDE_ROOT)/algos \
	-I$(RKAIQ_INCLUDE_ROOT)/iq_parser \
	-I$(RKAIQ_INCLUDE_ROOT)/iq_parser_v2

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
	-L$(RKAIQ_LIB) \
	-Wl,-rpath-link,$(RKAIQ_LIB) \
	-Wl,-rpath,/oem/usr/lib

LDLIBS += \
	-lrockiva \
	-lrknnmrt \
	-lrkaiq \
	-lstdc++ \
	-lpthread \
	-ldl \
	-lrt \
	-lm

.PHONY: all clean check-config print-config strip

all: check-config $(TARGET)

check-config:
	@test -f "$(PROJECT_ROOT)/capture/v4l2_capture.c" || \
		{ echo "Missing $(PROJECT_ROOT)/capture/v4l2_capture.c"; exit 1; }
	@test -f "$(PROJECT_ROOT)/isp/isp_control.c" || \
		{ echo "Missing $(PROJECT_ROOT)/isp/isp_control.c"; exit 1; }
	@test -f "$(PROJECT_ROOT)/common/log.c" || \
		{ echo "Missing $(PROJECT_ROOT)/common/log.c"; exit 1; }
	@test -f "$(ROCKIVA_INCLUDE)/rockiva_common.h" || \
		{ echo "Missing RockIVA headers under $(ROCKIVA_INCLUDE)"; exit 1; }
	@test -f "$(ROCKIVA_LIB)/librockiva.so" || \
		{ echo "Missing librockiva.so under $(ROCKIVA_LIB)"; exit 1; }
	@test -n "$(RKAIQ_ROOT)" || \
		{ echo "Could not auto-detect RKAIQ_ROOT"; exit 1; }
	@test -f "$(RKAIQ_INCLUDE_ROOT)/uAPI2/rk_aiq_user_api2_sysctl.h" || \
		{ echo "Missing RKAIQ headers under $(RKAIQ_INCLUDE_ROOT)"; exit 1; }
	@test -f "$(RKAIQ_LIB)/librkaiq.so" || \
		{ echo "Missing librkaiq.so under $(RKAIQ_LIB)"; exit 1; }

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Built $@"

$(BUILD_DIR)/main.o: main.c live_person_detector.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/live_person_detector.o: \
	live_person_detector.c live_person_detector.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/v4l2_capture.o: \
	$(PROJECT_ROOT)/capture/v4l2_capture.c \
	$(PROJECT_ROOT)/capture/v4l2_capture.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isp_control.o: \
	$(PROJECT_ROOT)/isp/isp_control.c \
	$(PROJECT_ROOT)/isp/isp_control.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/log.o: \
	$(PROJECT_ROOT)/common/log.c \
	$(PROJECT_ROOT)/include/log.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

print-config:
	@echo "CC                  = $(CC)"
	@echo "PROJECT_ROOT        = $(PROJECT_ROOT)"
	@echo "SDK_ROOT            = $(SDK_ROOT)"
	@echo "ROCKIVA_ROOT        = $(ROCKIVA_ROOT)"
	@echo "RKAIQ_ROOT          = $(RKAIQ_ROOT)"
	@echo "TARGET              = $(TARGET)"

strip: all
	$(STRIP) $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
