#include "Logger.h"
#include "Windows.h"
namespace Hagine {
namespace Logger {
void Log(const std::string &message) {
    OutputDebugStringA(message.c_str());
}
} // namespace Logger
} // namespace Hagine
