# mini_rtsp_server build for Luckfox Pico Max / RV1106

PROJECT := mini_rtsp_server
VERSION ?= 2.4.0-rc1
BUILD ?= release

CROSS_COMPILE ?= arm-rockchip830-linux-uclibcgnueabihf-
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
STRIP ?= $(CROSS_COMPILE)strip

SDK_ROOT ?= $(HOME)/boards/rk1116/sdk/luckfox-pico

MPP_RELEASE ?= $(SDK_ROOT)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf
BOARD_MPP_LIB ?= $(HOME)/boards/rk1116/mpp_board_abi

MEDIA_OUT_INCLUDE ?= $(SDK_ROOT)/media/out/include
RKMUXER_INCLUDE ?= $(MEDIA_OUT_INCLUDE)
RKMUXER_LIB ?= $(HOME)/boards/rk1116/rkmuxer_reference/lib

RKAIQ_INCLUDE_ROOT ?= $(MEDIA_OUT_INCLUDE)/rkaiq
RKAIQ_LIB ?= $(SDK_ROOT)/media/out/lib

BUILD_DIR := build/$(BUILD)
TARGET := $(BUILD_DIR)/bin/$(PROJECT)

SOURCES := \
	main.c \
	common/log.c \
	capture/v4l2_capture.c \
	encoder/mpp_encoder.c \
	h264/h264_annexb.c \
	isp/isp_control.c \
	muxer/mp4_muxer.c \
	rtp/rtp_packet.c \
	rtp/rtp_sender.c \
	rtp/h264_rtp.c \
	rtsp/rtsp_request.c \
	rtsp/rtsp_session.c \
	rtsp/rtsp_media.c \
	rtsp/rtsp_server.c \
	sdp/base64.c \
	sdp/sdp_builder.c

OBJECTS := $(addprefix $(BUILD_DIR)/obj/,$(SOURCES:.c=.o))
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += \
	-Iinclude \
	-Icapture \
	-Iencoder \
	-Ih264 \
	-Iisp \
	-Imuxer \
	-Irtp \
	-Irtsp \
	-Isdp \
	-I$(MPP_RELEASE)/include \
	-I$(RKMUXER_INCLUDE) \
	-I$(RKAIQ_INCLUDE_ROOT) \
	-I$(RKAIQ_INCLUDE_ROOT)/uAPI2 \
	-I$(RKAIQ_INCLUDE_ROOT)/common \
	-I$(RKAIQ_INCLUDE_ROOT)/xcore \
	-I$(RKAIQ_INCLUDE_ROOT)/algos \
	-I$(RKAIQ_INCLUDE_ROOT)/iq_parser \
	-I$(RKAIQ_INCLUDE_ROOT)/iq_parser_v2 \
	-DPROJECT_VERSION=\"$(VERSION)\"

COMMON_CFLAGS := \
	-std=c11 \
	-Wall \
	-Wextra \
	-Werror \
	-D_POSIX_C_SOURCE=200809L \
	-MMD \
	-MP

ifeq ($(BUILD),debug)
CFLAGS += $(COMMON_CFLAGS) -O0 -g3
else ifeq ($(BUILD),release)
CFLAGS += $(COMMON_CFLAGS) -O2 -DNDEBUG
else
$(error BUILD must be release or debug)
endif

LDFLAGS += \
	-L$(BOARD_MPP_LIB) \
	-Wl,-rpath-link,$(BOARD_MPP_LIB) \
	-L$(RKMUXER_LIB) \
	-Wl,-rpath-link,$(RKMUXER_LIB) \
	-L$(RKAIQ_LIB) \
	-Wl,-rpath-link,$(RKAIQ_LIB) \
	-Wl,-rpath,/oem/usr/lib

LDLIBS += \
	-lrockchip_mpp \
	-lrkmuxer \
	-lrkaiq \
	-lpthread \
	-lm \
	-ldl \
	-lstdc++

PREFIX ?= /usr/local
DESTDIR ?=

.PHONY: all clean distclean check-config print-config strip install help

all: check-config $(TARGET)

check-config:
	@test -f "$(MPP_RELEASE)/include/rockchip/rk_mpi.h" || \
		{ echo "Missing MPP headers under MPP_RELEASE=$(MPP_RELEASE)"; exit 1; }
	@test -f "$(BOARD_MPP_LIB)/librockchip_mpp.so" || \
		{ echo "Missing board ABI library under BOARD_MPP_LIB=$(BOARD_MPP_LIB)"; exit 1; }
	@test -f "$(RKMUXER_INCLUDE)/rkmuxer.h" || \
		{ echo "Missing rkmuxer.h under RKMUXER_INCLUDE=$(RKMUXER_INCLUDE)"; exit 1; }
	@test -f "$(RKMUXER_LIB)/librkmuxer.so" || \
		{ echo "Missing librkmuxer.so under RKMUXER_LIB=$(RKMUXER_LIB)"; exit 1; }
	@test -f "$(RKAIQ_INCLUDE_ROOT)/uAPI2/rk_aiq_user_api2_sysctl.h" || \
		{ echo "Missing RKAIQ headers under RKAIQ_INCLUDE_ROOT=$(RKAIQ_INCLUDE_ROOT)"; exit 1; }
	@test -f "$(RKAIQ_LIB)/librkaiq.so" || \
		{ echo "Missing librkaiq.so under RKAIQ_LIB=$(RKAIQ_LIB)"; exit 1; }

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Built $@"

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

print-config:
	@echo "PROJECT             = $(PROJECT)"
	@echo "VERSION             = $(VERSION)"
	@echo "BUILD               = $(BUILD)"
	@echo "CC                  = $(CC)"
	@echo "TARGET              = $(TARGET)"
	@echo "SDK_ROOT            = $(SDK_ROOT)"
	@echo "MPP_RELEASE         = $(MPP_RELEASE)"
	@echo "BOARD_MPP_LIB       = $(BOARD_MPP_LIB)"
	@echo "RKMUXER_INCLUDE     = $(RKMUXER_INCLUDE)"
	@echo "RKMUXER_LIB         = $(RKMUXER_LIB)"
	@echo "RKAIQ_INCLUDE_ROOT  = $(RKAIQ_INCLUDE_ROOT)"
	@echo "RKAIQ_LIB           = $(RKAIQ_LIB)"

strip: all
	$(STRIP) $(TARGET)
	@echo "Stripped $(TARGET)"

install: all
	install -D -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)

clean:
	rm -rf build

# Keep this alias for CI or release scripts that expect a stronger clean target.
distclean: clean

help:
	@echo "Targets:"
	@echo "  make                     Build release binary"
	@echo "  make BUILD=debug         Build with -O0 -g3"
	@echo "  make print-config        Show resolved SDK/library paths"
	@echo "  make strip               Strip the release/debug binary"
	@echo "  make install DESTDIR=... Install under PREFIX/bin"
	@echo "  make clean               Remove all build outputs"
	@echo ""
	@echo "Output: $(TARGET)"

-include $(DEPS)
