#pragma once
#include <QSize>
#include <rhi/qrhi.h>
#include <QMatrix4x4>
#include <memory>
#include <controller/Frame.h>

namespace view {
class ShaderResource {
public:
    enum class Type {
        Yuv,
        D3D11
    };

    struct Params {
        Type type{Type::Yuv};
        QRhi* rhi{nullptr};
        QSize size{0, 0};
#ifdef _WIN32
        // 裸指针安全声明：只借用，不管理生命周期
        // void*类型安全声明：必须由调用者保证类型为 ID3D11Device*
        void* d3d11Device{nullptr};
        // 裸指针安全声明：只借用，不管理生命周期
        // void*类型安全声明：必须由调用者保证类型为 ID3D11DeviceContext*
        void* d3d11DeviceContext{nullptr};
#endif
        controller::IFrame::PixelFormat swPixelFormat{controller::IFrame::PixelFormat::None};
    };
    static std::unique_ptr<ShaderResource> create(const Params& params);

    ShaderResource(const Params& params);
    virtual ~ShaderResource() = default;

    // 本次创建的帧尺寸，由 initialize() 从 Params 记录，派生类无需各自维护
    // 保留 virtual 是为了给特殊实现留出覆写余地，默认实现即返回 m_size
    virtual QSize size() const { return m_size; }
    virtual QRhiShaderResourceBindings* shaderResourceBinding() const = 0;

    // 三个矩阵uniform的公共默认实现：写入基类持有的dynamic uniform buffer。
    // 派生类若使用不同的绑定位置/缓冲布局，可以覆写这几个函数
    virtual void updateVertexTransformMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);
    virtual void updateColorRangeConversionMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);
    virtual void updateYUVtoRGBMatrix(QRhiResourceUpdateBatch* rub, const QMatrix4x4& matrix);

    virtual void updateTexture(QRhiResourceUpdateBatch* rub, const controller::FramePtr& frame) = 0;

    virtual Type type() const { return m_type; }

    controller::IFrame::PixelFormat swPixelFormat() const { return m_swPixelFormat; }
protected:
    // 模板方法(非虚)：固定执行顺序 —— 校验参数并保存 m_rhi/m_size → 创建公共的矩阵uniform buffer
    // → 回调派生类的 initializeResources()。派生类因此不可能漏建公共资源，
    // 且 SRB 创建时这些 buffer 必定已就绪(工厂 create() 只调这一个入口)
    bool initialize();

private:
    // 派生类只需初始化自己的资源(纹理、采样器等)，不必关心公共 buffer
    virtual bool initializeResources() = 0;

    // 仅供 initialize() 使用；设为 private，派生类无法(也不需要)重复创建
    bool createMatrixBuffers();

protected:
    Type m_type; // 记录具体的 ShaderResource 类型
    QRhi* m_rhi{nullptr}; // 由 initialize() 校验并赋值，派生类可直接使用
    QSize m_size; // 帧尺寸，由 initialize() 从 Params 记录；失败时保持无效尺寸(QSize 默认构造)
    controller::IFrame::PixelFormat m_swPixelFormat{controller::IFrame::PixelFormat::None};
    std::unique_ptr<QRhiBuffer> m_vsVertexTransformMatrix; // 对应video.vert中的UBuf变量，绑定位置3
    std::unique_ptr<QRhiBuffer> m_fsColorRangeConversionMatrix; // 对应yuv420p.frag中的ColorRangeBlock变量，绑定位置4，在着色器中先乘
    std::unique_ptr<QRhiBuffer> m_fsYUVtoRGBMatrix; // 对应yuv420p.frag中的YuvToRGBBlock变量，绑定位置5，在着色器中后乘
};
} // namespace view