#include "SubnetBroadcaster.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <chrono>
#include <thread>
#include "MessageCodec.hpp"

SubnetBroadcaster::SubnetBroadcaster(unsigned int interval_ms, uint16_t port)
    : interval_ms_(interval_ms), port_(port), running_(false), sockfd_(-1), alive_msg_(MessageCodec::MSG_ALIVE), shutdown_msg_(MessageCodec::MSG_SHUTDOWN), include_hostname_(true) {}

SubnetBroadcaster::~SubnetBroadcaster() {
    stop();
}

bool SubnetBroadcaster::init(const std::string& if_name) {
    if (!find_interface_broadcast(if_name, broadcast_addr_)) {
        std::cerr << "Не удалось определить широковещательный адрес интерфейса.\n";
        return false;
    }

    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        perror("socket");
        return false;
    }

    int on = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0) {
        perror("setsockopt SO_BROADCAST");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port_);
    if (inet_pton(AF_INET, broadcast_addr_.c_str(), &dest_.sin_addr) != 1) {
        std::cerr << "inet_pton failed for " << broadcast_addr_ << "\n";
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    return true;
}

bool SubnetBroadcaster::start(uint8_t alive_msg, uint8_t shutdown_msg, bool include_hostname) {
    if (sockfd_ < 0) {
        std::cerr << "Socket not initialized. Call init() first.\n";
        return false;
    }
    if (running_.exchange(true)) {
        std::cerr << "Already running\n";
        return false;
    }
    alive_msg_ = alive_msg;
    shutdown_msg_ = shutdown_msg;
    include_hostname_ = include_hostname;

    // resolve local hostname once
    char hn[256] = {0};
    if (gethostname(hn, sizeof(hn)) == 0) hostname_ = hn;

    worker_ = std::thread(&SubnetBroadcaster::run_loop, this);
    return true;
}

void SubnetBroadcaster::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

bool SubnetBroadcaster::send_now(uint8_t code, const std::string& payload) {
    if (sockfd_ < 0) return false;
    if (payload.empty()) {
        uint8_t buf = code;
        ssize_t r = sendto(sockfd_, &buf, sizeof(buf), 0,
                           reinterpret_cast<struct sockaddr*>(&dest_), sizeof(dest_));
        return r == (ssize_t)sizeof(buf);
    }

    // build buffer: 1 byte code + payload bytes
    std::string out;
    out.reserve(1 + payload.size());
    out.push_back(static_cast<char>(code));
    out.append(payload.data(), payload.size());

    ssize_t r = sendto(sockfd_, out.data(), out.size(), 0,
                       reinterpret_cast<struct sockaddr*>(&dest_), sizeof(dest_));
    return r == (ssize_t)out.size();
}

std::string SubnetBroadcaster::broadcast_address() const { return broadcast_addr_; }
uint16_t SubnetBroadcaster::port() const { return port_; }

void SubnetBroadcaster::run_loop() {
    while (running_.load()) {
        if (include_hostname_) {
            if (!send_now(alive_msg_, hostname_)) {
                std::cerr << "Failed to send alive message\n";
            }
        } else {
            if (!send_now(alive_msg_, std::string())) {
                std::cerr << "Failed to send alive message\n";
            }
        }
        unsigned int waited = 0;
        const unsigned int step = 50;
        while (running_.load() && waited < interval_ms_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
            waited += step;
        }
    }
    // send shutdown code if set (0xFF reserved to mean "no shutdown")
    if (shutdown_msg_ != 0xFF) {
        if (include_hostname_) {
            if (!send_now(shutdown_msg_, hostname_)) {
                std::cerr << "Failed to send shutdown message\n";
            }
        } else {
            if (!send_now(shutdown_msg_, std::string())) {
                std::cerr << "Failed to send shutdown message\n";
            }
        }
    }
}

bool SubnetBroadcaster::find_interface_broadcast(const std::string& if_name, std::string& out_bcast) {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return false;
    }

    bool found = false;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!if_name.empty() && if_name != ifa->ifa_name) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        struct sockaddr_in* netmask = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);
        if (!addr || !netmask) continue;

        uint32_t ip = ntohl(addr->sin_addr.s_addr);
        uint32_t mask = ntohl(netmask->sin_addr.s_addr);
        uint32_t bcast = (ip & mask) | (~mask);

        struct in_addr baddr;
        baddr.s_addr = htonl(bcast);
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &baddr, buf, sizeof(buf)) == nullptr) continue;

        out_bcast = buf;
        found = true;
        break;
    }

    freeifaddrs(ifaddr);
    if (!found && !if_name.empty()) {
        std::cerr << "Интерфейс " << if_name << " не найден или не подходит.\n";
    }
    return found;
}
