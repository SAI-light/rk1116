# RK1116 Embedded Vision & RTSP Project

基于 **Luckfox Pico Max / Rockchip RV1106** 平台实现的嵌入式视觉采集与智能视频监控项目，主要用于学习和实践嵌入式 Linux 摄像头、多媒体编解码、RTSP/RTP 网络传输以及 NPU 智能视觉分析完整链路。

项目从最初的 V4L2 图像采集、H.264 NALU 解析和单文件 RTSP 原型逐步演进为模块化工程，目前已经完成 **RKAIQ ISP、V4L2、MPP、RTP/RTSP、MP4 Muxer、RockIVA/NPU PERSON 检测及人员事件状态机** 等模块的开发与验证。

## Current Features

* **Camera Capture**：基于 V4L2 + MMAP 多缓冲区采集 2304×1296 NV12 视频
* **ISP Processing**：接入 RKAIQ，对 SC3336 摄像头进行 ISP 调校和启动预热
* **Hardware Encoding**：基于 Rockchip MPP / RV1106 VEPU 完成 H.264 30 fps 硬件编码
* **RTSP / RTP**：自主实现 RTSP 基础会话流程及 RTP over UDP H.264 传输
* **H.264 Packetization**：支持单 NALU 与 FU-A 分片，并完成 VLC 实时播放验证
* **MP4 Recording**：基于 RKMuxer 将实时 H.264 Access Unit 同步封装为 MP4
* **AI Detection**：基于 RockIVA + RV1106 NPU + PFP 模型实现 PERSON 检测
* **Event Tracking**：通过多帧状态机实现 PERSON ENTER / PRESENT / LEAVE 稳定判定
* **Engineering**：模块化 `.c/.h`、Makefile、日志、CLI、信号退出、测试工具及 Git 分支管理

## Media Pipeline

```text
SC3336 Camera
      │
      ▼
    RKAIQ
      │
      ▼
V4L2 / NV12 / MMAP
      │
      ▼
 Rockchip MPP
      │
      ▼
 H.264 Access Unit
      │
      ├──────────────► RTP / RTSP ─────► VLC
      │
      └──────────────► RKMuxer ────────► MP4
```

AI 检测目前通过独立实时测试链路完成：

```text
V4L2 NV12
    │
    ▼
DMA Buffer
    │
    ▼
RockIVA / RGA
    │
    ▼
RV1106 NPU + PFP
    │
    ▼
PERSON Detection
    │
    ▼
person_event
    │
    └── ENTER / PRESENT / LEAVE
```

## Repository Structure

```text
rk1116/
├── Makefile                    # Repository build entry
├── README.md
├── .gitignore
│
├── mini_rtsp_server_project/   # Main media project
│   ├── capture/
│   ├── common/
│   ├── encoder/
│   ├── event/
│   ├── h264/
│   ├── isp/
│   ├── muxer/
│   ├── rtp/
│   ├── rtsp/
│   ├── sdp/
│   ├── tests/
│   ├── tools/
│   ├── main.c
│   └── Makefile
│
└── sdk/                        # Luckfox / Rockchip SDK reference
```

详细的模块设计、编译方式和测试结果请参见：

**[mini_rtsp_server_project/README.md](mini_rtsp_server_project/README.md)**

## Build

在仓库根目录执行：

```bash
make
```

顶层 Makefile 会进入 `mini_rtsp_server_project` 调用主工程 Makefile。

清理：

```bash
make clean
```

成功编译后主程序输出位于：

```text
mini_rtsp_server_project/build/release/bin/mini_rtsp_server
```

> 编译环境需要配置与 RV1106 SDK 匹配的交叉编译工具链以及 Rockchip MPP、RKAIQ、RKMuxer 等相关库。

## Current Status

目前已经完成并验证：

**Camera → ISP → V4L2 → MPP → H.264 → RTP/RTSP / MP4**

同时完成：

**V4L2 → RockIVA → NPU PERSON Detection → person_event**

实时媒体链路已稳定运行，VLC 播放及 MP4 完整解码验证通过。PERSON 静态目标检测以及实时 AI 推理链路均已验证，实时场景下的 PERSON 正样本检测效果仍需结合摄像头画质和 AI 输入分辨率进一步测试。

## Roadmap

后续计划包括：

* 2304×1296 与 896×512 PERSON 检测 A/B 对照测试
* 基于 RGA 构建独立低分辨率 AI 输入支路
* 将 RockIVA + person_event 正式接入主媒体采集线程
* H.264 预录环形缓冲区与 PERSON 事件录像
* PIR、环境光与补光控制
* 事件文件管理及 SFTP / HTTPS 后台上传
* 长时间运行、异常恢复及系统稳定性测试

---

This repository is mainly used for embedded Linux multimedia development, Rockchip platform learning, and project practice.
