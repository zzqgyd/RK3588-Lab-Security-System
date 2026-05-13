
#ifndef INSPIRECV_LOGGING_H_
#define INSPIRECV_LOGGING_H_

#include <sstream>

#ifndef INSPIRECV_API
#define INSPIRECV_API
#endif

/**
 * @brief Macro to create a logging stream.
 */
#define INSPIRECV_LOG(severity)                                                           \
    inspirecv::logging::ISFLogging(__FILE__, __LINE__,                                    \
                                   inspirecv::logging::ISFLogging::LogSeverity::severity) \
      .Stream()

/**
 * @brief Macro to check if verbose logging is enabled for a given level.
 */
#define INSPIRECV_VLOG_IS_ON(verboselevel) \
    ((verboselevel) <= inspirecv::logging::ISFLogging::VLogLevel())

/**
 * @brief Macro to set the verbose logging level.
 */
#define INSPIRECV_VLOG_SET_LEVEL(verboselevel) \
    inspirecv::logging::ISFLogging::VLogSetLevel(verboselevel)

/**
 * @brief Macro to log a message if verbose logging is enabled for a given level.
 */
#define INSPIRE_VLOG(verboselevel) LOG_IF(INFO, INSPIRECV_VLOG_IS_ON(verboselevel))

namespace inspirecv {
namespace logging {
/**
 * @brief A wrapper that logs to stderr.
 */
class INSPIRECV_API ISFLogging {
public:
    enum class LogSeverity : int {
        INFO = 0,
        WARN = 1,
        ERROR = 2,
        FATAL = 3,
    };
    ISFLogging(const char *filename, int line, LogSeverity severity)
    : severity_(severity), filename_(filename), line_(line) {}
    std::stringstream &Stream() {
        return stream_;
    }
    ~ISFLogging();

    static int VLogLevel();
    static void VLogSetLevel(int level);

private:
    std::stringstream stream_;
    LogSeverity severity_;
    const char *filename_;
    int line_;
    static int vlog_level_;
};

}  // namespace logging

}  // namespace inspirecv

#endif  // INSPIRECV_LOGGING_H_
