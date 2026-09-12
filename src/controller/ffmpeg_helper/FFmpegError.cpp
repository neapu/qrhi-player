#include "FFmpegError.h"

extern "C" {
#include <libavutil/error.h>
}

namespace fh {
std::string err2str(int errorCode)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

} // namespace fh