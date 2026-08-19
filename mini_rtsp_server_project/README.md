# Mini RTSP Server Project

基于 **Luckfox Pico Max / Rockchip RV1106** 实现的嵌入式摄像头采集、H.264 硬件编码、RTSP/RTP 实时推流、MP4 同步录像与智能 PERSON 检测工程。

项目采用模块化 C 语言设计，将摄像头采集、ISP、编码、H.264 解析、RTP、RTSP、SDP、MP4 Muxer 和智能事件检测拆分为独立模块，用于理解和实践嵌入式 Linux 多媒体系统完整数据链路。

## 1. System Architecture

当前主媒体链路：

```text
                     SC3336
                        │
                        ▼
                     RKAIQ
                        │
                        ▼
              /dev/video11 / V4L2
                        │
                 2304×1296 NV12
                        │
                        ▼
                Rockchip MPP
                RV1106 VEPU
                        │
                        ▼
                H.264 Access Unit
                        │
             ┌──────────┴──────────┐
             │                     │
             ▼                     ▼
        RTP Packetizer          RKMuxer
             │                     │
       Single NALU / FU-A           ▼
             │                    MP4
             ▼
         RTP over UDP
             │
             ▼
         RTSP Session
             │
             ▼
            VLC
```

独立 AI 验证链路：

```text
V4L2 2304×1296 NV12
        │
        ▼
  DMA-backed Buffer
        │
        ▼
     RockIVA
        │
     RGA Preprocess
        │
        ▼
librknnmrt / RV1106 NPU
        │
        ▼
  PFP PERSON Detection
        │
        ▼
   person_event
        │
        ├── ENTER
        ├── PRESENT
        └── LEAVE
```

AI 检测未采用 OpenCV 作为核心推理框架，而是使用 Rockchip 平台提供的 **RockIVA + RGA + NPU** 硬件加速链路。

## 2. Implemented Features

### Camera & ISP

* `/dev/video11` V4L2 Video Capture Multiplanar
* NV12 图像格式
* 2304×1296 分辨率
* MMAP 多缓冲区连续采集
* 实际采集约 30 fps
* RKAIQ ISP 初始化、Prepare、Start、Stop、Deinit
* SC3336 Sensor IQ 参数加载
* 启动阶段丢弃约 30 帧完成曝光和 ISP 预热

### H.264 Hardware Encoding

基于 **Rockchip MPP / RV1106 VEPU**：

* NV12 → H.264 硬件编码
* 2304×1296 @ 30 fps
* GOP 约 30
* SPS / PPS 提取
* IDR 与普通 NALU 解析
* H.264 Access Unit 输出

### RTSP

自主实现基础 RTSP Server 流程，包括：

```text
OPTIONS
DESCRIBE
SETUP
PLAY
GET_PARAMETER
TEARDOWN
```

DESCRIBE 阶段动态构建 H.264 SDP，并携带 SPS/PPS 参数。

### RTP

实现 RTP over UDP H.264 发送：

* RTP sequence number
* 90 kHz timestamp
* SSRC
* Marker bit
* H.264 单 NALU Packet
* H.264 FU-A 分片

已通过 VLC 完成实时播放验证。

### MP4 Recording

基于 **RKMuxer** 对同一路 MPP H.264 Access Unit 进行 MP4 封装：

```text
MPP
 │
 ▼
H.264 Access Unit
 │
 ├── RTP / RTSP
 │
 └── RKMuxer → MP4
```

已使用 `ffprobe` / `ffmpeg` 对录像进行帧率、帧数、时长以及完整解码验证。

### RockIVA PERSON Detection

通过独立工具 `tools/live_person_detect_test/` 完成实时 PERSON 检测链路：

* RockIVA VIDEO 模式
* PFP Detection Model
* PERSON-only Object Filter
* DMA-backed NV12 输入
* 双 AI Buffer Slot
* 异步 RockIVA Callback
* AI 采样约 5 fps
* 单次推理典型延迟约 39–40 ms
* AI Buffer busy 时直接跳过采样，不阻塞 V4L2 视频采集

静态 PERSON 测试已得到有效正向检测结果。

实时摄像头链路能够稳定完成 RockIVA 推理与 Callback，但当前实际监控画面尚未获得稳定 PERSON 正检测样本，后续将重点验证摄像头画质、人物尺寸以及输入分辨率对检测结果的影响。

### PERSON Event State Machine

`event/person_event.c/.h` 将单帧 PERSON 检测结果转换为稳定业务事件。

默认规则：

```text
ENTER：
最近 3 次 AI 检测中至少 2 次检测到 PERSON

LEAVE：
连续 5 次 AI 检测未检测到 PERSON
```

状态包括：

```text
IDLE
CANDIDATE
PRESENT
EXIT_PENDING
```

业务事件包括：

```text
NONE
ENTER
PRESENT
LEAVE
```

该模块已完成独立单元测试，并已接入 `live_person_detect_test` 的实时 RockIVA Callback。

## 3. Project Structure

```text
mini_rtsp_server_project/
├── capture/               # V4L2 camera capture
├── common/                # Common utilities / logging
├── docs/                  # Project documents
├── encoder/               # Rockchip MPP H.264 encoder
├── event/                 # PERSON event state machine
├── h264/                  # H.264 parsing / reader
├── include/               # Common project headers
├── isp/                   # RKAIQ ISP control
├── muxer/                 # RKMuxer MP4 wrapper
├── rtp/                   # RTP packetization / sender
├── rtsp/                  # RTSP protocol / session
├── scripts/               # Development / cleanup scripts
├── sdp/                   # SDP generation / Base64
│
├── tests/
│   ├── README.md
│   └── integration/       # Cross-module integration tests
│
├── tools/
│   ├── live_person_detect_test/
│   ├── person_event_test/
│   └── person_pet_detect_test/
│
├── main.c                 # Main application entry
├── Makefile
├── README.md
├── CHANGELOG.md
└── .gitignore
```

部分模块级测试仍保留在对应模块目录中，例如：

```text
capture/test_capture.c
encoder/test_*.c
rtp/test_*.c
sdp/test_*.c
```

跨模块媒体链路测试统一整理至：

```text
tests/integration/
```

## 4. Build

推荐从仓库根目录：

```bash
cd ~/boards/rk1116
make
```

也可以直接进入项目目录：

```bash
cd mini_rtsp_server_project
make
```

清理：

```bash
make clean
```

成功编译后：

```text
build/release/bin/mini_rtsp_server
```

程序应为 RV1106 对应 ARM/uClibc 可执行文件。

可通过：

```bash
file build/release/bin/mini_rtsp_server
```

检查目标架构。

## 5. Runtime Verification

主工程目前已经完成以下验证：

```text
V4L2 Camera Capture
        ↓
RKAIQ ISP
        ↓
MPP H.264 Encoding
        ↓
RTP / RTSP
        ↓
VLC Playback
```

同时：

```text
H.264 Access Unit
        ↓
RKMuxer
        ↓
MP4
        ↓
ffprobe / ffmpeg
```

以及独立 AI 链路：

```text
V4L2
  ↓
RockIVA
  ↓
RV1106 NPU
  ↓
PERSON
  ↓
person_event
```

## 6. Engineering Design

当前工程重点遵循：

* 摄像头设备只由单一采集模块管理
* 视频采集和 AI 推理采用不同处理节奏
* AI 推理不能阻塞 30 fps 视频采集
* 编码数据统一以 H.264 Access Unit 向 RTP 与 MP4 分发
* 模块通过 `.c/.h` 接口隔离
* 生成物统一放入 `build/`
* 测试、工具、正式业务代码分别管理
* Git 功能开发采用 feature branch + Pull Request 合并流程

## 7. Known Limitations

目前仍存在以下待验证问题：

1. 当前实验室实时摄像头场景尚未稳定产生 PERSON 正检测结果；
2. 需要进一步区分摄像头失焦/画面模糊、人物尺寸与 RockIVA 输入分辨率的影响；
3. 当前 RockIVA/person_event 已在独立实时检测工具中接通，尚未正式并入主 `mini_rtsp_server` 数据链路；
4. PERSON 事件目前尚未驱动自动录像和服务器上传。

## 8. Roadmap

### AI Input Optimization

进行：

```text
2304×1296
vs
896×512
```

PERSON 实时检测 A/B 对照实验。

若低分辨率输入检测效果更好，则考虑正式设计：

```text
                     V4L2 2304×1296
                           │
                  ┌────────┴────────┐
                  │                 │
                  ▼                 ▼
                 MPP               RGA
                  │                 │
                  ▼                 ▼
          H.264 / RTSP / MP4      896×512
                                    │
                                    ▼
                                  RockIVA
                                    │
                                    ▼
                                   NPU
```

### Event Recording

将 `RockIVA + person_event` 正式接入主采集链路，并进一步实现：

```text
Continuous H.264
       │
       ▼
Pre-record Ring Buffer
       │
 PERSON ENTER
       │
       ▼
Pre-roll + Event + Post-roll
       │
       ▼
      MP4
```

### System Integration

后续计划：

* PIR 人体感应
* 环境光检测
* 自动补光
* PERSON 事件录像
* 本地录像文件管理
* SFTP / HTTPS 后台上传
* 上传失败重试
* 磁盘空间管理
* 开机自启动与异常恢复
* 长时间稳定性测试

## 9. Development Status

当前阶段已经完成核心视频媒体链路和基础智能视觉链路验证。

由于现阶段开发重点转向嵌入式 Linux、C/C++、网络、多媒体和项目面试准备，系统功能扩展暂时冻结；后续将继续完成 AI 输入优化、事件录像以及传感器/服务器侧集成。

更多历史变更参见：

**[CHANGELOG.md](CHANGELOG.md)**
