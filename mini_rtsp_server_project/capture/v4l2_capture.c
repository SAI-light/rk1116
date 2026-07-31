/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: v4l2_capture.c
 * Description: V4L2 multi-planar MMAP capture for one-plane NV12 frames.
 ********************************************************************************/

#include "v4l2_capture.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#define MODULE_NAME "capture"
#define TARGET_CAPTURE_FPS 25U

static int xioctl(int fd,
                  unsigned long request,
                  void *arg)
{
    int ret;

    do
    {
        ret = ioctl(fd, request, arg);
    }
    while (ret < 0 && errno == EINTR);

    return ret;
}

static void fourcc_to_string(uint32_t fourcc,
                             char text[5])
{
    text[0] = (char)(fourcc & 0xffU);
    text[1] = (char)((fourcc >> 8) & 0xffU);
    text[2] = (char)((fourcc >> 16) & 0xffU);
    text[3] = (char)((fourcc >> 24) & 0xffU);
    text[4] = '\0';
}

static int wait_for_frame(int fd,
                          int timeout_ms)
{
    for (;;)
    {
        fd_set read_fds;
        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;
        int ret;

        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        if (timeout_ms >= 0)
        {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            timeout_ptr = &timeout;
        }

        ret = select(fd + 1, &read_fds, NULL, NULL, timeout_ptr);
        if (ret > 0)
            return 0;

        if (ret == 0)
        {
            errno = ETIMEDOUT;
            return -1;
        }

        if (errno != EINTR)
            return -1;
    }
}

int v4l2_capture_init(V4L2Capture *cap,
                      const char *device,
                      int width,
                      int height)
{
    struct v4l2_capability capability;
    struct v4l2_format format;
    struct v4l2_streamparm streamparm;
    struct v4l2_requestbuffers request;
    enum v4l2_buf_type type;
    unsigned int i;
    char format_text[5];

    if (cap == NULL || device == NULL || width <= 0 || height <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    memset(cap, 0, sizeof(*cap));
    cap->fd = -1;

    cap->fd = open(device, O_RDWR | O_NONBLOCK);
    if (cap->fd < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "open camera %s failed", device);
        return -1;
    }

    memset(&capability, 0, sizeof(capability));
    if (xioctl(cap->fd, VIDIOC_QUERYCAP, &capability) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_QUERYCAP failed");
        goto FAIL;
    }

    {
        uint32_t caps = capability.capabilities;

        if ((caps & V4L2_CAP_DEVICE_CAPS) != 0U)
            caps = capability.device_caps;

        if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0U)
        {
            LOG_ERROR(MODULE_NAME,
                      "device does not support multi-planar video capture");
            errno = ENOTSUP;
            goto FAIL;
        }

        if ((caps & V4L2_CAP_STREAMING) == 0U)
        {
            LOG_ERROR(MODULE_NAME,
                      "device does not support streaming I/O");
            errno = ENOTSUP;
            goto FAIL;
        }
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.width = (uint32_t)width;
    format.fmt.pix_mp.height = (uint32_t)height;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (xioctl(cap->fd, VIDIOC_S_FMT, &format) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_S_FMT failed");
        goto FAIL;
    }

    cap->width = (int)format.fmt.pix_mp.width;
    cap->height = (int)format.fmt.pix_mp.height;
    cap->nplanes = (int)format.fmt.pix_mp.num_planes;
    cap->pixelformat = format.fmt.pix_mp.pixelformat;
    cap->bytesperline = (int)format.fmt.pix_mp.plane_fmt[0].bytesperline;
    cap->sizeimage = (int)format.fmt.pix_mp.plane_fmt[0].sizeimage;

    fourcc_to_string(cap->pixelformat, format_text);
    LOG_INFO(MODULE_NAME,
             "format: device=%s size=%dx%d fourcc=%s planes=%d stride=%d sizeimage=%d",
             device,
             cap->width,
             cap->height,
             format_text,
             cap->nplanes,
             cap->bytesperline,
             cap->sizeimage);

    if (cap->pixelformat != V4L2_PIX_FMT_NV12)
    {
        LOG_ERROR(MODULE_NAME, "camera did not accept NV12 format");
        errno = ENOTSUP;
        goto FAIL;
    }

    if (cap->nplanes != 1)
    {
        LOG_ERROR(MODULE_NAME,
                  "encoder path requires one contiguous NV12 plane; driver returned %d",
                  cap->nplanes);
        errno = ENOTSUP;
        goto FAIL;
    }

    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    streamparm.parm.capture.timeperframe.numerator = 1U;
    streamparm.parm.capture.timeperframe.denominator = TARGET_CAPTURE_FPS;

    if (xioctl(cap->fd, VIDIOC_S_PARM, &streamparm) < 0)
    {
        LOG_WARN_ERRNO(MODULE_NAME,
                       errno,
                       "VIDIOC_S_PARM %u fps is not supported; using driver timing",
                       TARGET_CAPTURE_FPS);
        cap->fps_numerator = 0U;
        cap->fps_denominator = 0U;
    }
    else
    {
        cap->fps_numerator = streamparm.parm.capture.timeperframe.numerator;
        cap->fps_denominator = streamparm.parm.capture.timeperframe.denominator;
    }

    if (cap->fps_numerator != 0U && cap->fps_denominator != 0U)
    {
        LOG_INFO(MODULE_NAME,
                 "frame interval: %u/%u s (%.2f fps)",
                 cap->fps_numerator,
                 cap->fps_denominator,
                 (double)cap->fps_denominator /
                 (double)cap->fps_numerator);
    }
    else
    {
        LOG_INFO(MODULE_NAME,
                 "frame interval not reported by driver; pipeline target=%u fps",
                 TARGET_CAPTURE_FPS);
    }

    memset(&request, 0, sizeof(request));
    request.count = V4L2_CAPTURE_MAX_BUFFERS;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    request.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cap->fd, VIDIOC_REQBUFS, &request) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_REQBUFS failed");
        goto FAIL;
    }

    if (request.count < 2U || request.count > V4L2_CAPTURE_MAX_BUFFERS)
    {
        LOG_ERROR(MODULE_NAME,
                  "unexpected V4L2 buffer count: %u",
                  request.count);
        errno = ENOMEM;
        goto FAIL;
    }

    cap->buffer_count = request.count;

    for (i = 0U; i < cap->buffer_count; ++i)
    {
        struct v4l2_buffer buffer;
        struct v4l2_plane planes[V4L2_CAPTURE_MAX_PLANES];

        memset(&buffer, 0, sizeof(buffer));
        memset(planes, 0, sizeof(planes));

        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        buffer.length = (uint32_t)cap->nplanes;
        buffer.m.planes = planes;

        if (xioctl(cap->fd, VIDIOC_QUERYBUF, &buffer) < 0)
        {
            LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_QUERYBUF failed");
            goto FAIL;
        }

        cap->buffers[i].length = planes[0].length;
        cap->buffers[i].addr = mmap(NULL,
                                    cap->buffers[i].length,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED,
                                    cap->fd,
                                    planes[0].m.mem_offset);

        if (cap->buffers[i].addr == MAP_FAILED)
        {
            cap->buffers[i].addr = NULL;
            LOG_ERROR_ERRNO(MODULE_NAME,
                            errno,
                            "mmap camera buffer %u failed",
                            i);
            goto FAIL;
        }

        if (xioctl(cap->fd, VIDIOC_QBUF, &buffer) < 0)
        {
            LOG_ERROR_ERRNO(MODULE_NAME,
                            errno,
                            "VIDIOC_QBUF during init failed");
            goto FAIL;
        }

        LOG_DEBUG(MODULE_NAME,
                  "buffer %u: addr=%p length=%zu",
                  i,
                  cap->buffers[i].addr,
                  cap->buffers[i].length);
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(cap->fd, VIDIOC_STREAMON, &type) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_STREAMON failed");
        goto FAIL;
    }

    cap->streaming = 1;
    return 0;

FAIL:
    v4l2_capture_close(cap);
    return -1;
}

int v4l2_capture_acquire_frame(V4L2Capture *cap,
                               V4L2Frame *frame,
                               int timeout_ms)
{
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[V4L2_CAPTURE_MAX_PLANES];
    size_t data_offset;
    size_t bytes_used;

    if (cap == NULL || frame == NULL || cap->fd < 0 || !cap->streaming)
    {
        errno = EINVAL;
        return -1;
    }

    memset(frame, 0, sizeof(*frame));

    for (;;)
    {
        if (wait_for_frame(cap->fd, timeout_ms) < 0)
            return -1;

        memset(&buffer, 0, sizeof(buffer));
        memset(planes, 0, sizeof(planes));

        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.length = (uint32_t)cap->nplanes;
        buffer.m.planes = planes;

        if (xioctl(cap->fd, VIDIOC_DQBUF, &buffer) == 0)
            break;

        if (errno != EAGAIN)
        {
            LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_DQBUF failed");
            return -1;
        }
    }

    if (buffer.index >= cap->buffer_count)
    {
        LOG_ERROR(MODULE_NAME,
                  "driver returned invalid buffer index %u",
                  buffer.index);
        errno = EIO;
        return -1;
    }

    data_offset = planes[0].data_offset;
    bytes_used = planes[0].bytesused;

    if (data_offset > cap->buffers[buffer.index].length ||
        bytes_used < data_offset ||
        bytes_used > cap->buffers[buffer.index].length)
    {
        V4L2Frame invalid_frame;

        LOG_ERROR(MODULE_NAME,
                  "invalid plane metadata: index=%u bytesused=%zu data_offset=%zu mapped=%zu",
                  buffer.index,
                  bytes_used,
                  data_offset,
                  cap->buffers[buffer.index].length);

        memset(&invalid_frame, 0, sizeof(invalid_frame));
        invalid_frame.index = buffer.index;
        invalid_frame.valid = 1;
        (void)v4l2_capture_release_frame(cap, &invalid_frame);

        errno = EIO;
        return -1;
    }

    frame->data = (uint8_t *)cap->buffers[buffer.index].addr + data_offset;
    frame->size = bytes_used - data_offset;
    frame->index = buffer.index;
    frame->sequence = buffer.sequence;
    frame->timestamp_us =
        (int64_t)buffer.timestamp.tv_sec * 1000000LL +
        (int64_t)buffer.timestamp.tv_usec;
    frame->valid = 1;

    return 0;
}

int v4l2_capture_release_frame(V4L2Capture *cap,
                               V4L2Frame *frame)
{
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[V4L2_CAPTURE_MAX_PLANES];

    if (cap == NULL || frame == NULL || cap->fd < 0 || !frame->valid)
    {
        errno = EINVAL;
        return -1;
    }

    if (frame->index >= cap->buffer_count)
    {
        errno = EINVAL;
        return -1;
    }

    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));

    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = frame->index;
    buffer.length = (uint32_t)cap->nplanes;
    buffer.m.planes = planes;

    if (xioctl(cap->fd, VIDIOC_QBUF, &buffer) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "VIDIOC_QBUF release failed");
        return -1;
    }

    memset(frame, 0, sizeof(*frame));
    return 0;
}

int v4l2_capture_get_frame(V4L2Capture *cap,
                           uint8_t **data,
                           int *size)
{
    V4L2Frame frame;
    uint8_t *new_copy;
    int release_ret;
    size_t copied_size;

    if (cap == NULL || data == NULL || size == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (v4l2_capture_acquire_frame(cap, &frame, 3000) < 0)
        return -1;

    if (frame.size > cap->legacy_copy_capacity)
    {
        new_copy = (uint8_t *)realloc(cap->legacy_copy, frame.size);
        if (new_copy == NULL)
        {
            LOG_ERROR(MODULE_NAME,
                      "realloc legacy frame copy failed, size=%zu",
                      frame.size);
            (void)v4l2_capture_release_frame(cap, &frame);
            return -1;
        }

        cap->legacy_copy = new_copy;
        cap->legacy_copy_capacity = frame.size;
    }

    copied_size = frame.size;
    memcpy(cap->legacy_copy, frame.data, copied_size);

    release_ret = v4l2_capture_release_frame(cap, &frame);
    if (release_ret < 0)
        return -1;

    *data = cap->legacy_copy;
    *size = (int)copied_size;

    return 0;
}

void v4l2_capture_close(V4L2Capture *cap)
{
    unsigned int i;

    if (cap == NULL)
        return;

    if (cap->fd >= 0 && cap->streaming)
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (xioctl(cap->fd, VIDIOC_STREAMOFF, &type) < 0)
        {
            LOG_WARN_ERRNO(MODULE_NAME,
                           errno,
                           "VIDIOC_STREAMOFF failed during cleanup");
        }

        cap->streaming = 0;
    }

    for (i = 0U; i < cap->buffer_count; ++i)
    {
        if (cap->buffers[i].addr != NULL && cap->buffers[i].length > 0U)
        {
            munmap(cap->buffers[i].addr, cap->buffers[i].length);
            cap->buffers[i].addr = NULL;
            cap->buffers[i].length = 0U;
        }
    }

    cap->buffer_count = 0U;

    if (cap->fd >= 0)
    {
        close(cap->fd);
        cap->fd = -1;
    }

    free(cap->legacy_copy);
    cap->legacy_copy = NULL;
    cap->legacy_copy_capacity = 0U;
}
