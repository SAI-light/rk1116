# rk1116
# RK1116 / Luckfox RV1106 摄像头监控开发仓库

本仓库用于记录基于 **Luckfox Pico Max（RV1106）** 的 Linux 嵌入式开发与摄像头监控项目实践。当前重点完成了从摄像头原始图像采集、MPP 硬件 H.264 编码，到自研 RTSP/RTP 实时推流和 MP4 同步录像的完整底层视频链路。

> 当前工程状态：`v2.4.0-rc1`  
> 已验证稳定链路：V4L2 → MPP H.264 → RTP/RTSP + MP4

---

## 项目目标

项目按照以下方向逐步推进：

1. 熟悉开发板 SDK、交叉编译、烧录和板端运行；
2. 参考官方 Demo，验证摄像头和 VLC 远程监控；
3. 学习 Linux V4L2 摄像头采集与缓冲区管理；
4. 使用 Rockchip MPP 调用 RV1106 硬件编码器；
5. 自主实现 RTSP 控制、RTP/H.264 封包和 VLC 实时播放；
6. 将同一份 H.264 数据同步封装为 MP4 文件；
7. 后续结合 Qt/LVGL、OpenCV、YOLO 或人脸识别，扩展为完整摄像头监控系统。

---

## 当前实现

当前核心工程已经实现：

- 使用 V4L2 从 `/dev/video11` 采集 NV12 图像；
- 使用 MMAP 和多缓冲区完成连续视频采集；
- 使用 Rockchip MPP 调用 RV1106 VEPU 硬件编码 H.264；
- 从实时 H.264 码流中解析 SPS、PPS、IDR 和普通 NALU；
- 动态生成 SDP；
- 实现 RTSP `OPTIONS`、`DESCRIBE`、`SETUP`、`PLAY`、`GET_PARAMETER` 和 `TEARDOWN`；
- 支持 RTP over UDP；
- 支持 H.264 单 NALU 和 FU-A 分片；
- VLC 可通过 RTSP 地址实时播放；
- 同一份 H.264 Access Unit 同步用于 RTP 推流和 MP4 录像；
- PLAY 后丢弃陈旧摄像头缓冲区，并等待新的 IDR 后开始推流和录像；
- 支持统一日志级别和命令行参数；
- 支持 `SIGINT`、`SIGTERM` 退出框架；
- 使用 FFprobe 和 FFmpeg 完成 MP4 帧数、时长与完整解码验证。

---

## 数据链路

```text
Camera Sensor
      │
      ▼
Rockchip ISP
      │
      ▼
/dev/video11
      │
      ▼
V4L2 MMAP（NV12）
      │
      ▼
Rockchip MPP / RV1106 VEPU
      │
      ▼
H.264 Access Unit
      │
      ├──────────────► RTP/H.264 ─► UDP ─► VLC
      │
      └──────────────► RKMuxer ───► MP4 文件
```

项目中不需要先保存 `.yuv` 或 `.h264` 中间文件。V4L2、MPP、RTP 和 RKMuxer 之间通过内存中的“数据指针 + 数据长度 + 时间戳 + 帧类型”传递视频数据。

当前链路属于内存实时处理，但 V4L2 与 MPP 输入缓冲区之间仍可能存在内存复制，并不是严格意义上的全链路零拷贝。

---

## 核心参数

| 参数           | 当前配置               |
| -------------- | ---------------------- |
| 开发板         | Luckfox Pico Max       |
| SoC            | Rockchip RV1106        |
| 摄像头节点     | `/dev/video11`         |
| 输入格式       | NV12                   |
| 分辨率         | 2304 × 1296            |
| 编码格式       | H.264                  |
| 编码方式       | MPP 调用 VEPU 硬件编码 |
| 目标帧率       | 25 fps                 |
| GOP            | 25                     |
| 目标码率       | 4 Mbps                 |
| RTSP 端口      | 8554                   |
| RTP 服务器端口 | 5000                   |
| RTP 时钟       | 90000 Hz               |
| MP4 时间基准   | 微秒                   |

---

## 仓库结构

```text
.
├── README.md
├── mini_rtsp_server_project/    # 当前核心摄像头、RTSP、RTP 与 MP4 工程
└── sdk/
    └── luckfox-pico/            # Luckfox SDK（子模块或本地 SDK）
```

核心工程内部主要模块：

```text
mini_rtsp_server_project/
├── main.c                       # 程序入口、参数解析、日志和退出控制
├── capture/                     # V4L2 摄像头采集
├── encoder/                     # Rockchip MPP H.264 编码
├── h264/                        # Annex-B、SPS/PPS/IDR 解析
├── rtp/                         # RTP Header、H.264 FU-A 和 UDP 发送
├── rtsp/                        # RTSP Server、Session 和媒体线程
├── sdp/                         # SDP 动态生成
├── muxer/                       # RKMuxer MP4 封装
├── common/                      # 通用日志等基础模块
├── include/                     # 公共头文件
├── docs/                        # 验证记录和工程说明
├── scripts/                     # 工作区清理等辅助脚本
├── Makefile
└── README.md                    # 核心工程的详细编译与运行说明
```

详细设计、编译参数和板端运行方式请查看：

[`mini_rtsp_server_project/README.md`](mini_rtsp_server_project/README.md)

---

## 编译

进入核心工程：

```bash
cd mini_rtsp_server_project
```

查看当前配置：

```bash
make print-config
```

编译 Release 版本：

```bash
make clean
make
```

生成文件：

```text
build/release/bin/mini_rtsp_server
```

查看帮助和版本：

```bash
build/release/bin/mini_rtsp_server --help
build/release/bin/mini_rtsp_server --version
```

---

## 传输到开发板

```bash
scp build/release/bin/mini_rtsp_server \
root@<BOARD_IP>:/root/
```

板端停止可能占用摄像头的官方服务：

```bash
killall rkipc 2>/dev/null
chmod +x /root/mini_rtsp_server
```

---

## 运行

推流并同步录像：

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
/root/mini_rtsp_server \
    --port 8554 \
    --device /dev/video11 \
    --record /root/live_record.mp4 \
    --log-level info
```

只推流、不录像：

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
/root/mini_rtsp_server \
    --port 8554 \
    --device /dev/video11 \
    --no-record \
    --log-level info
```

VLC 打开：

```text
rtsp://<BOARD_IP>:8554/live
```

---

## MP4 验证

将录像传回 Ubuntu：

```bash
scp root@<BOARD_IP>:/root/live_record.mp4 ./
```

检查视频信息和实际帧数：

```bash
ffprobe \
    -v error \
    -count_frames \
    -select_streams v:0 \
    -show_entries \
stream=codec_name,profile,width,height,avg_frame_rate,nb_read_frames:format=format_name,duration,size \
    -of default=noprint_wrappers=1 \
    live_record.mp4
```

完整解码检查：

```bash
ffmpeg \
    -v error \
    -i live_record.mp4 \
    -f null -
```

FFmpeg 没有错误输出，说明视频能够被完整解析和解码。

---

## 已验证结果

稳定测试结果之一：

```text
codec_name=h264
profile=Constrained Baseline
width=2304
height=1296
avg_frame_rate=25/1
nb_read_frames=363
duration=14.520000
```

同时验证：

- VLC 实时画面正常；
- 363 帧全部写入 MP4；
- 正常媒体阶段 V4L2 `sequence_gaps=0`；
- GOP 25，对应 15 个关键帧；
- VLC `TEARDOWN` 后 MP4 正常完成；
- FFmpeg 完整解码无错误。

---

## 版本说明

### `v2.3`

完成并验证核心稳定链路：

```text
V4L2 → MPP H.264 → RTP/RTSP + MP4
```

### `v2.4.0-rc1`

完成工程化收尾：

- 统一日志级别；
- 整理命令行参数；
- 优化 Makefile；
- 增加 README、CHANGELOG 和验证文档；
- 增加 `SIGINT`、`SIGTERM` 退出框架；
- 清理历史调试源码和迁移文件。

VLC `TEARDOWN` 安全关闭已经验证。首次 `Ctrl+C` 录像测试因板端存储空间不足触发 `ENOSPC`，因此该次生成的 MP4 缺少 `moov atom`，不能作为正常信号退出结果；后续需要在存储空间充足时重新验证。

---

## 当前限制

当前版本仍属于学习和验证性质的单客户端视频服务，主要限制包括：

- 仅支持单个 RTSP 客户端；
- 当前媒体传输为 RTP over UDP；
- 尚未实现 RTCP 完整交互；
- 尚未实现 RTSP 用户认证；
- 尚未实现多线程采集、编码、推流和录像队列；
- 尚未实现循环录像和磁盘空间自动管理；
- 尚未集成 Qt/LVGL 图形界面；
- 尚未集成 OpenCV、YOLO 或人脸识别；
- 尚未完成产品级守护进程、配置文件和日志轮转。

---

## 后续方向

结合导师后续安排，可在当前底层视频链路上继续扩展：

- Qt 或 LVGL 监控界面；
- 实时预览、录像控制、抓拍和回放；
- 循环录像与磁盘空间管理；
- RTP over TCP；
- 多客户端 RTSP；
- RTCP 与网络质量统计；
- OpenCV 图像处理；
- RKNN / NPU 上的 YOLO 或人脸检测；
- 检测框、时间和设备信息 OSD；
- 异常事件告警；
- 开机自启动和守护进程；
- V4L2 DMA-BUF 到 MPP 的零拷贝优化。

---

## 学习重点

本项目涉及的主要知识点：

- Linux V4L2 与摄像头驱动；
- MMAP、多缓冲区和 Buffer 生命周期；
- NV12 图像格式；
- Rockchip MPP 与 VEPU 硬件编码；
- H.264 Annex-B、SPS、PPS、IDR 和 NALU；
- RTP Header 与 FU-A 分片；
- RTSP 会话和 SDP；
- Socket、TCP 和 UDP；
- MP4 容器与时间戳；
- 交叉编译、动态链接和嵌入式调试。

---

## 说明

本仓库用于嵌入式 Linux 和摄像头监控项目的学习、实验与阶段性开发。核心代码仍在持续整理和完善，当前实现不建议直接用于生产环境。
