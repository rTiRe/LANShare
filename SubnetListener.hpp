#ifndef SUBNET_LISTENER_HPP
#define SUBNET_LISTENER_HPP

#include <string>
#include <cstdint>
#include "MessageCodec.hpp"
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <netinet/in.h>

struct DeviceInfo {
    std::string ip;
    std::string hostname;
    uint8_t lastMessage;
    std::chrono::steady_clock::time_point lastSeen;
};

class SubnetListener {
public:
    explicit SubnetListener(uint16_t port = 40000);
    ~SubnetListener();

    bool start();
    void stop();

    std::unordered_map<std::string, DeviceInfo> get_devices();

private:
    uint16_t port_;
    int sockfd_;
    std::atomic<bool> running_;
    std::thread worker_;
    std::mutex devices_mutex_;
    std::unordered_map<std::string, DeviceInfo> devices_;

    void listen_loop();
};

#endif // SUBNET_LISTENER_HPP
