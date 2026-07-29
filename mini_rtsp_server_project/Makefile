# Live RTSP + MP4 build for Luckfox Pico Max / RV1106

CC := arm-rockchip830-linux-uclibcgnueabihf-gcc

MPP_RELEASE ?= $(HOME)/boards/rk1116/sdk/luckfox-pico/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf
BOARD_MPP_LIB ?= $(HOME)/boards/rk1116/mpp_board_abi
RKMUXER_INCLUDE ?= $(HOME)/boards/rk1116/sdk/luckfox-pico/media/out/include
RKMUXER_LIB ?= $(HOME)/boards/rk1116/rkmuxer_reference/lib

TARGET := mini_rtsp_server
BUILD_DIR := build

SOURCES := \
	main.c \
	capture/v4l2_capture.c \
	encoder/mpp_encoder.c \
	h264/h264_annexb.c \
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

OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))

CPPFLAGS += \
	-Icapture \
	-Iencoder \
	-Ih264 \
	-Imuxer \
	-Irtp \
	-Irtsp \
	-Isdp \
	-I$(MPP_RELEASE)/include \
	-I$(RKMUXER_INCLUDE)

CFLAGS := -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L

LDFLAGS += \
	-L$(BOARD_MPP_LIB) \
	-Wl,-rpath-link,$(BOARD_MPP_LIB) \
	-L$(RKMUXER_LIB) \
	-Wl,-rpath-link,$(RKMUXER_LIB) \
	-Wl,-rpath,/oem/usr/lib

LDLIBS += -lrockchip_mpp -lrkmuxer -lpthread -lm -ldl

.PHONY: all clean check-config

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

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
