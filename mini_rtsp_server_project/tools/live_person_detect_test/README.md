# live_person_detect_test

RV1106 独立实时摄像头 PERSON 检测测试程序。

## 当前范围

- RKAIQ 常驻运行；
- `/dev/video11` 连续采集 2304×1296 NV12；
- 丢弃前 30 帧等待曝光稳定；
- 摄像头约 30 fps；
- 每秒向 RockIVA 提交约 5 帧；
- RockIVA 使用 `ROCKIVA_MODE_VIDEO`；
- 两个独立 RockIVA DMA 缓冲区，AI 忙时跳过检测样本而不阻塞采集；
- 仅请求和打印 `PERSON` 类；
- 暂不接入 RTSP、录像、红外传感器和文件上传。

板端继续使用已经验证的模型：

```text
/root/rockiva_model/object_detection_pfp.data
```

模型文件本身是 PFP 模型，但检测结果位掩码只启用 PERSON，因此业务回调不会返回 PET。

## 工程位置

将本目录放到：

```text
mini_rtsp_server_project/tools/live_person_detect_test
```

Makefile 直接复用主工程中已经验证的：

```text
capture/v4l2_capture.c
isp/isp_control.c
common/log.c
```

## 编译

```bash
cd ~/boards/rk1116/mini_rtsp_server_project/tools/live_person_detect_test

make print-config
make clean
make
```

## 传输

```bash
scp build/live_person_detect_test \
root@172.32.0.93:/root/
```

## 板端运行

先停止占用摄像头的系统程序：

```bash
killall rkipc 2>/dev/null
```

先运行 30 秒：

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
/root/live_person_detect_test \
  --device /dev/video11 \
  --iq-dir /oem/usr/share/iqfiles \
  --model-dir /root/rockiva_model \
  --detect-fps 5 \
  --threshold 60 \
  --warmup-frames 30 \
  --duration 30
```

持续运行到按下 Ctrl+C：

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
/root/live_person_detect_test
```

## 预期输出

画面中没有人：

```text
[AI] frame_id=1 camera_sequence=... status=0 person_count=0 latency_ms=...
```

检测到人：

```text
[AI] frame_id=... camera_sequence=... status=0 person_count=1 latency_ms=...
[AI]   PERSON obj_id=... score=... box_norm=(...)-(...) box_px=(...)-(...)
```

每约 5 秒打印状态：

```text
[status] captured=... sequence_gaps=... AI_submitted=... AI_busy_skips=... AI_results=... person_frames=...
```

首次实机测试重点检查：

- `frame_id` 持续递增；
- `camera_sequence` 持续递增；
- `sequence_gaps` 是否为 0；
- `AI_busy_skips` 是否为 0 或很少；
- 有人时是否输出 PERSON、score 和检测框；
- Ctrl+C 后 RKAIQ、V4L2、RockIVA 是否正常释放。
