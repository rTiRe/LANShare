#include "SubnetListener.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <chrono>

SubnetListener::SubnetListener(uint16_t port)
    : port_(port), sockfd_(-1), running_(false) {}

SubnetListener::~SubnetListener() {
    stop();
}

bool SubnetListener::start() {
    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        perror("socket");
        return false;
    }

    int on = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    running_.store(true);
    worker_ = std::thread(&SubnetListener::listen_loop, this);
    return true;
}

void SubnetListener::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
        ::close(sockfd_);
        sockfd_ = -1;
    }
    if (worker_.joinable()) worker_.join();
}

void SubnetListener::listen_loop() {
    uint8_t buffer[1024];
    struct sockaddr_in sender{};
    socklen_t sender_len = sizeof(sender);

    while (running_.load()) {
        ssize_t bytes = recvfrom(sockfd_, buffer, sizeof(buffer), 0,
                                 reinterpret_cast<struct sockaddr*>(&sender), &sender_len);
        if (bytes <= 0) {
            if (running_.load()) perror("recvfrom");
            break;
        }

        // First byte is message code. If there are extra bytes, treat them as sender-provided hostname.
        uint8_t code = static_cast<uint8_t>(buffer[0]);

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip_str, sizeof(ip_str));
        std::string ip = ip_str;

        std::string payload_hostname;
        if (bytes > 1) {
            // payload bytes after the first byte represent hostname (no validation currently)
            payload_hostname.assign(reinterpret_cast<char*>(buffer + 1), bytes - 1);
        }

        // Получаем hostname via reverse lookup only if payload didn't include it
        char hostbuf[NI_MAXHOST];
        if (payload_hostname.empty()) {
            if (getnameinfo(reinterpret_cast<struct sockaddr*>(&sender), sender_len,
                            hostbuf, sizeof(hostbuf), nullptr, 0, NI_NAMEREQD) != 0) {
                std::strcpy(hostbuf, "unknown");
            }
        } else {
            std::strncpy(hostbuf, payload_hostname.c_str(), sizeof(hostbuf) - 1);
            hostbuf[sizeof(hostbuf) - 1] = '\0';
        }

        DeviceInfo info;
        info.ip = ip;
        info.hostname = hostbuf;
        info.lastMessage = code;
        info.lastSeen = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            devices_[ip] = info;
        }

    std::cout << "[RECV] code=" << static_cast<int>(code)
          << " (" << MessageCodec::name_for(code) << ") from " << ip
          << " (" << hostbuf << ")" << std::endl;
    }
}

std::unordered_map<std::string, DeviceInfo> SubnetListener::get_devices() {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    return devices_;
}
