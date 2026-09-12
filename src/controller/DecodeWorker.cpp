#include "DecodeWorker.h"

namespace controller {
std::unique_ptr<DecodeWorker> DecodeWorker::create(const AVStream* stream)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker());
    if (worker->initialize(stream)) {
        return worker;
    }
    return nullptr;
}

bool DecodeWorker::initialize(const AVStream* stream)
{
    // Initialization logic for the decode worker goes here.
    // Return true if initialization is successful, false otherwise.
    return true;
}
} // namespace controller