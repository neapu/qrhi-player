#include "ShaderResource.h"
#include "YuvShaderResource.h"

namespace view {
std::unique_ptr<ShaderResource> ShaderResource::create(const Params& params)
{
    switch (params.type) {
    case Type::Yuv:
        {
            std::unique_ptr<ShaderResource> res = std::make_unique<YuvShaderResource>();
            if (res->initialize(params)) {
                return res;
            }
        }
        break;
    }
    return nullptr;
}
} // namespace view