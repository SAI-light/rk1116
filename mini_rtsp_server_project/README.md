# mini_rtsp_server_project

基于 Luckfox Pico Max（RV1106）的轻量级实时视频工程，目标是在板端完成：

```text
摄像头采集（V4L2 / NV12）
        ↓
RV1106 MPP 硬件 H.264 编码
        ├──→ Rockchip Muxer 实时封装 MP4
        └──→ RTP 封包 / RTSP 服务 → VLC 播放
```

项目使用 C 语言按功能拆分模块，主要用于学习并实现 Linux V4L2、Rockchip MPP、H.264、RTP、RTSP、SDP 和 MP4 封装的完整数据链路。

---

## 当前状态

| 模块 | 状态 | 已验证结果 |
|---|---|---|
| V4L2 摄像头采集 | 已完成 | `/dev/video11`，2304×1296，NV12，V4L2 多平面 MMAP |
| NV12 → H.264 硬件编码 | 已完成 | RV1106 MPP/VENC，Constrained Baseline，25 fps，GOP 25 |
| 连续 H.264 输出 | 已完成 | 100 帧及 300 帧连续编码测试通过 |
| H.264 → MP4 实时封装 | 已完成 | 使用 `librkmuxer.so`，中间不落盘裸 H.264 |
| 长时间稳定性 | 已完成 | 60 秒、1500 帧、0 帧序号缺口、25.00 fps |
| MP4 完整性验证 | 已完成 | `ffprobe` 识别 1500 帧、60 秒；`ffmpeg` 完整解码无错误 |
| RTSP 控制流程 | 已完成基础实现 | OPTIONS、DESCRIBE、SETUP、PLAY、TEARDOWN |
| SDP 生成 | 已完成基础实现 | H.264、SPS/PPS、`track1` |
| RTP/H.264 封包 | 已完成基础实现 | RTP Header、UDP 发送、单 NALU、FU-A 分片测试 |
| VLC 联调 | 已完成阶段性测试 | RTSP/RTP 基础链路及 H.264 文件数据测试 |
| 实时摄像头接入 RTSP | 待集成 | 将 MPP 输出的内存 H.264 直接送入 RTP/RTSP 模块 |

### 当前稳定录像参数

```text
分辨率：2304×1296
像素格式：NV12
编码格式：H.264 Constrained Baseline
帧率：25 fps
GOP：25
目标码率：4 Mbps
```

60 秒测试结果：

```text
encoded frames      : 1500/1500
key frames          : 60
sequence gaps       : 0
timeline duration   : 60.000 s
end-to-end speed    : 25.00 fps
```

---

## 项目结构

```text
.
├── capture/                 # V4L2 摄像头采集
│   ├── v4l2_capture.c
│   ├── v4l2_capture.h
│   └── test_capture.c
├── encoder/                 # RV1106 MPP H.264 硬件编码
│   ├── mpp_encoder.c
│   ├── mpp_encoder.h
│   ├── test_mpp_init.c
│   ├── test_encode_nv12.c
│   ├── test_encode_continuous.c
│   └── test_create_only.c
├── muxer/                   # Rockchip MP4 封装
│   ├── mp4_muxer.c
│   └── mp4_muxer.h
├── h264/                    # H.264 Annex-B 读取与解析
│   ├── h264_reader.c
│   └── h264_reader.h
├── rtp/                     # RTP Header、H.264 RTP、FU-A 和 UDP 发送
│   ├── rtp_packet.c
│   ├── rtp_packet.h
│   ├── rtp_sender.c
│   ├── rtp_sender.h
│   ├── h264_rtp.c
│   ├── h264_rtp.h
│   └── test_*.c
├── rtsp/                    # RTSP 请求解析、会话与服务器
│   ├── rtsp_request.c
│   ├── rtsp_request.h
│   ├── rtsp_session.c
│   ├── rtsp_session.h
│   ├── rtsp_media.c
│   ├── rtsp_media.h
│   ├── rtsp_server.c
│   └── rtsp_server.h
├── sdp/                     # Base64 与 SDP 构建
│   ├── base64.c
│   ├── base64.h
│   ├── sdp_builder.c
│   ├── sdp_builder.h
│   └── test_*.c
├── main.c                   # RTSP 服务入口
├── test_camera_encode.c     # 摄像头实时编码 H.264 测试
├── test_camera_mp4_v2.c     # 摄像头实时编码并封装 MP4 测试
├── README.md
└── .gitignore
```

> 仓库只保留当前有效源码。可执行文件、目标文件、日志、YUV/H.264/MP4 测试数据及历史源码副本不应提交；稳定版本使用 Git commit 和 tag 保存，而不是复制整个目录。

---

## 运行环境

### 开发主机

- Ubuntu 24.04
- Luckfox Pico SDK
- 交叉编译器：

```text
arm-rockchip830-linux-uclibcgnueabihf-gcc
```

### 开发板

- Luckfox Pico Max / RV1106
- ARMv7 / uClibc
- 摄像头节点：`/dev/video11`
- 板端运行库：

```text
/oem/usr/lib/librockchip_mpp.so.1
/oem/usr/lib/librkmuxer.so
```

---

## 外部依赖

本仓库不提交 SDK 头文件、动态库或交叉工具链。请通过环境变量指定本地路径。

示例：

```bash
export CC=arm-rockchip830-linux-uclibcgnueabihf-gcc

export MPP_RELEASE=$HOME/boards/rk1116/sdk/luckfox-pico/media/mpp/\
release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf

export BOARD_MPP_LIB=$HOME/boards/rk1116/mpp_board_abi

export RKMUXER_INCLUDE=$HOME/boards/rk1116/sdk/luckfox-pico/media/out/include

export RKMUXER_LIB=$HOME/boards/rk1116/rkmuxer_reference/lib
```

---

## 编译摄像头实时 MP4 测试

当前仓库尚未加入统一的 Makefile/CMake；以下命令用于编译已经验证的实时 MP4 测试程序。

```bash
$CC -Wall -Wextra -O2 \
    -I./capture \
    -I./encoder \
    -I./muxer \
    -I"$MPP_RELEASE/include" \
    -c test_camera_mp4_v2.c \
    -o test_camera_mp4_v2.o

$CC -Wall -Wextra -O2 \
    -I./capture \
    -c capture/v4l2_capture.c \
    -o v4l2_capture.o

$CC -Wall -Wextra -O2 \
    -I./encoder \
    -I"$MPP_RELEASE/include" \
    -c encoder/mpp_encoder.c \
    -o mpp_encoder.o

$CC -Wall -Wextra -O2 \
    -I./muxer \
    -I"$RKMUXER_INCLUDE" \
    -c muxer/mp4_muxer.c \
    -o mp4_muxer.o

$CC \
    test_camera_mp4_v2.o \
    v4l2_capture.o \
    mpp_encoder.o \
    mp4_muxer.o \
    -L"$BOARD_MPP_LIB" \
    -Wl,-rpath-link,"$BOARD_MPP_LIB" \
    -L"$RKMUXER_LIB" \
    -Wl,-rpath-link,"$RKMUXER_LIB" \
    -Wl,-rpath,/oem/usr/lib \
    -lrockchip_mpp \
    -lrkmuxer \
    -lpthread \
    -lm \
    -ldl \
    -o test_camera_mp4_v2
```

确认版本：

```bash
strings test_camera_mp4_v2 | grep "vendor-packet-v9.1-ring-boundary"
```

---

## 板端运行

系统自带的 `rkipc` 可能占用 `/dev/video11`，运行自研程序前先停止它：

```bash
killall rkipc 2>/dev/null
```

将程序传到板端：

```bash
scp test_camera_mp4_v2 root@<BOARD_IP>:/root/
```

录制 60 秒：

```bash
cd /root
chmod +x test_camera_mp4_v2

LD_LIBRARY_PATH=/oem/usr/lib \
./test_camera_mp4_v2 \
/dev/video11 \
output_camera_60s.mp4 \
1500
```

---

## MP4 验证

将文件传回 Ubuntu 后检查：

```bash
ffprobe \
    -v error \
    -count_frames \
    -select_streams v:0 \
    -show_entries \
stream=codec_name,profile,width,height,avg_frame_rate,nb_read_frames:\
format=format_name,duration,size \
    -of default=noprint_wrappers=1 \
    output_camera_60s.mp4
```

完整解码验证：

```bash
ffmpeg \
    -v error \
    -i output_camera_60s.mp4 \
    -f null -
```

正常情况下 `ffmpeg` 不输出错误信息。

---

## RTSP/RTP 当前实现

RTSP 模块此前已经完成以下基础流程：

```text
VLC                    RTSP Server
 |---- OPTIONS --------->|
 |<--- 200 OK -----------|
 |---- DESCRIBE -------->|
 |<--- SDP --------------|
 |---- SETUP ------------>|
 |<--- Transport/Session-|
 |---- PLAY ------------->|
 |<--- RTP/H.264 --------|
 |---- TEARDOWN --------->|
```

已实现或验证的功能包括：

- RTSP 请求解析与 CSeq 处理；
- Session 和 Transport 信息处理；
- H.264 SDP 描述；
- SPS/PPS Base64；
- RTP Header；
- H.264 单 NALU RTP 封包；
- FU-A 分片；
- RTP over UDP 阶段性联调；
- VLC 客户端基础测试。

下一阶段不是重新实现 RTSP，而是完成数据源替换：

```text
旧数据源：H.264 测试文件
新数据源：mpp_encoder_encode() 返回的内存 H.264
```

最终计划：

```text
V4L2 NV12
    ↓
MPP H.264
    ├──→ RTP/RTSP → VLC
    └──→ MP4 Muxer → 本地录像
```

---

## Git 仓库规范

### 应提交

- `.c`、`.h`
- `README.md`
- `.gitignore`
- 后续新增的 `Makefile` 或 `CMakeLists.txt`
- 必要的设计文档和协议说明
- 小型、明确授权的测试配置文件

### 不应提交

- 可执行文件与 `.o`
- SDK 动态库和静态库
- 编码后的 H.264/MP4
- 原始 NV12/YUV
- 日志文件
- 临时备份源码
- `stable_*` 整目录副本
- tar/zip 归档
- core dump
- 本地 IDE 配置

稳定版本应使用 Git tag，例如：

```bash
git tag -a camera-mp4-v9.1 -m "Stable camera H264 and MP4 pipeline"
git push origin camera-mp4-v9.1
```

---

## 后续计划

1. 增加统一 Makefile；
2. 清理历史源码副本和根目录构建产物；
3. 将实时 MPP H.264 接入现有 RTP/RTSP 模块；
4. 实现同一 H.264 Packet 同时用于 RTSP 推流和 MP4 录像；
5. VLC 验证 2304×1296、25 fps 实时播放；
6. 统计端到端延迟、CPU 和内存占用；
7. 视性能需要研究 V4L2 DMABUF → MPP 零拷贝。

---

## License

当前仓库尚未指定开源许可证。若计划公开发布，请根据代码来源和厂商 SDK 许可要求补充合适的 `LICENSE` 文件。
