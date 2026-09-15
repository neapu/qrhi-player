#pragma once
#include <memory>

namespace controller {
class IFrame {
public:
    enum class FrameType {
        Normal,
        End     // 用来标识流结尾
    };

    virtual ~IFrame() = default;

    /**
     * @brief 获取帧类型
     * @return FrameType 帧类型
     */
    virtual FrameType type() const = 0;

    /**
     * @brief 获取帧的序列号
     * @return int 帧的序列号
     */
    virtual int serial() const = 0;

    /**
     * @brief 获取帧的显示时间戳，单位为微秒(us)
     * @return int64_t 显示时间戳，无效时为AV_NOPTS_VALUE(-2^63)
     */
    virtual int64_t pts() const = 0;

    // ============= 视频相关 =============
    enum class PixelFormat {
        None,
        YUV420P,
    };
    enum class ColorSpace {
        BT601, // 其他全部退化到BT601
        BT709,
    };
    enum class ColorRange {
        Limited,
        Full,
    };

    /**
     * @brief 获取帧的宽度
     * @return int 帧的宽度
     */
    virtual int width() const = 0;
    /**
     * @brief 获取帧的高度
     * @return int 帧的高度
     */
    virtual int height() const = 0;
    /**
     * @brief 获取帧的像素格式
     * @return PixelFormat 帧的像素格式
     */
    virtual PixelFormat pixelFormat() const = 0;
    /**
     * @brief 获取帧的色彩空间
     * @return ColorSpace 帧的色彩空间
     */
    virtual ColorSpace colorSpace() const = 0;
    /**
     * @brief 获取帧的色彩范围
     * @return ColorRange 帧的色彩范围
     */
    virtual ColorRange colorRange() const = 0;

    /**
     * @brief 获取对应平面的数据指针，仅适用于YUV420P，相当于AVFrame中的data[0/1/2]字段
     * @return uint8_t* 对应平面的数据指针
     */
    virtual uint8_t* yData() const = 0;
    virtual uint8_t* uData() const = 0;
    virtual uint8_t* vData() const = 0;
    
    /**
     * @brief 获取对应平面的行大小，仅适用于YUV420P，相当于AVFrame中的linesize[0/1/2]字段
     * @return int 对应平面的行大小
     */
    virtual int yLineSize() const = 0;
    virtual int uLineSize() const = 0;
    virtual int vLineSize() const = 0;

    // ============= 音频相关 =============
    enum class SampleFormat {
        None,
        S16LE,  // 16-bit signed little-endian
    };

    /**
     * @brief 获取音频的采样格式
     * @return SampleFormat 音频的采样格式
     */
    virtual SampleFormat sampleFormat() const = 0;
    /**
     * @brief 获取音频的采样率
     * @return int 音频的采样率
     */
    virtual int sampleRate() const = 0;
    /**
     * @brief 获取音频的通道数
     * @return int 音频的通道数
     */
    virtual int channels() const = 0;
    /**
     * @brief 获取音频帧的采样数
     * @return int 音频帧的采样数
     */
    virtual int samples() const = 0;

    /**
     * @brief 获取音频PCM数据指针，仅适用于打包采样格式(如S16LE)，相当于AVFrame中的data[0]字段
     * @return uint8_t* PCM数据指针
     */
    virtual uint8_t* audioData() const = 0;
    /**
     * @brief 获取音频PCM数据的字节数，等于采样数x声道数x每采样字节数
     * @return int PCM数据的字节数
     */
    virtual int audioDataSize() const = 0;

    /**
     * @brief 获取底层的AVFrame原始指针
     * @return void* 底层的AVFrame原始指针
     */
    virtual void* rawFrame() = 0;
    virtual const void* rawFrame() const = 0;

    /**
     * @brief 释放底层的AVFrame原始指针的所有权
     * @return void* 被释放的AVFrame原始指针
     */
    virtual void* release() = 0;
};
using FramePtr = std::shared_ptr<IFrame>;
} // controller namespace