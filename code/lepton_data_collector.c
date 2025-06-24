#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

// Lepton configuration
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // 16-bit pixels
#define VIDEO_DEVICE "/dev/video0"
#define I2C_DEVICE "/dev/i2c-1"
#define LEPTON_I2C_ADDR 0x2A
#define CAPTURE_DURATION 3600 // 1 hour in seconds

// CCI command IDs (from FLIR Lepton Software IDD)
#define LEPTON_CCI_SET_AGC_ENABLE 0x0104
#define LEPTON_CCI_SET_RADIOMETRY_ENABLE 0x0204

int lepton_cci_write(int fd, uint16_t command_id, uint16_t value) {
    uint8_t buf[4];
    buf[0] = (command_id >> 8) & 0xFF; // Command ID MSB
    buf[1] = command_id & 0xFF;        // Command ID LSB
    buf[2] = (value >> 8) & 0xFF;      // Value MSB
    buf[3] = value & 0xFF;             // Value LSB

    struct i2c_msg msg = {
        .addr = LEPTON_I2C_ADDR,
        .flags = 0,
        .len = 4,
        .buf = buf
    };
    struct i2c_rdwr_ioctl_data data = {
        .msgs = &msg,
        .nmsgs = 1
    };

    if (ioctl(fd, I2C_RDWR, &data) < 0) {
        perror("Failed to write CCI command");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int video_fd, i2c_fd;
    uint16_t frame[FRAME_WIDTH * FRAME_HEIGHT];
    char filename[64];
    time_t start_time, now;
    struct v4l2_format fmt;
    struct v4l2_buffer buf;

    // Open V4L2 device
    video_fd = open(VIDEO_DEVICE, O_RDWR);
    if (video_fd < 0) {
        perror("Failed to open video device");
        return -1;
    }

    // Set V4L2 format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = FRAME_WIDTH;
    fmt.fmt.pix.height = FRAME_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_Y16; // 16-bit grayscale
    if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set video format");
        close(video_fd);
        return -1;
    }

    // Open I2C device
    i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        close(video_fd);
        return -1;
    }

    // Configure Lepton: Disable AGC, Enable Radiometry
    if (lepton_cci_write(i2c_fd, LEPTON_CCI_SET_AGC_ENABLE, 0) < 0) {
        fprintf(stderr, "Failed to disable AGC\n");
    } else {
        printf("AGC disabled\n");
    }
    if (lepton_cci_write(i2c_fd, LEPTON_CCI_SET_RADIOMETRY_ENABLE, 1) < 0) {
        fprintf(stderr, "Failed to enable radiometry\n");
    } else {
        printf("Radiometry enabled\n");
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start streaming");
        close(i2c_fd);
        close(video_fd);
        return -1;
    }

    // Record start time
    start_time = time(NULL);
    printf("Capturing frames for 1 hour starting at %s", ctime(&start_time));

    while (1) {
        // Check elapsed time
        now = time(NULL);
        if (difftime(now, start_time) >= CAPTURE_DURATION) {
            printf("Capture duration (1 hour) reached. Stopping.\n");
            break;
        }

        // Read frame from V4L2
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("Failed to dequeue buffer");
            continue;
        }

        // Copy frame data
        ssize_t bytes_read = read(video_fd, frame, FRAME_SIZE);
        if (bytes_read != FRAME_SIZE) {
            fprintf(stderr, "Failed to read frame\n");
            if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
                perror("Failed to requeue buffer");
            }
            continue;
        }

        // Generate timestamped filename
        strftime(filename, sizeof(filename), "lepton_%Y%m%d_%H%M%S.grey", localtime(&now));
        
        // Save frame to .grey file
        FILE *file = fopen(filename, "wb");
        if (!file) {
            perror("Failed to open output file");
            if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
                perror("Failed to requeue buffer");
            }
            continue;
        }
        fwrite(frame, sizeof(uint16_t), FRAME_WIDTH * FRAME_HEIGHT, file);
        fclose(file);
        printf("Saved frame to %s\n", filename);

        // Requeue buffer
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to requeue buffer");
            break;
        }

        // Sleep to control frame rate (~9 FPS for Lepton)
        usleep(111111);
    }

    // Stop streaming
    if (ioctl(video_fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Failed to stop streaming");
    }

    close(i2c_fd);
    close(video_fd);
    return 0;
}