#ifndef MESSAGE_CODEC_HPP
#define MESSAGE_CODEC_HPP

#include <cstdint>
#include <string>

namespace MessageCodec {
    // well-known message codes
    constexpr uint8_t MSG_SHUTDOWN = 0;
    constexpr uint8_t MSG_ALIVE = 1;
    constexpr uint8_t MSG_CUSTOM = 10;

    inline std::string name_for(uint8_t code) {
        switch (code) {
            case MSG_SHUTDOWN: return "shutdown";
            case MSG_ALIVE: return "alive";
            case MSG_CUSTOM: return "custom";
            default: return "unknown";
        }
    }
}

#endif // MESSAGE_CODEC_HPP
