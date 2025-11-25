#ifndef SUBNET_BROADCASTER_HPP
#define SUBNET_BROADCASTER_HPP

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <netinet/in.h>

class SubnetBroadcaster {
public:
    SubnetBroadcaster(unsigned int interval_ms = 2000, uint16_t port = 40000);
    ~SubnetBroadcaster();

    bool init(const std::string& if_name = "");
    bool start(uint8_t alive_code = 1, uint8_t shutdown_code = 0, bool include_hostname = true);
    void stop();
    bool send_now(uint8_t code, const std::string& payload = std::string());

    std::string broadcast_address() const;
    uint16_t port() const;

private:
    unsigned int interval_ms_;
    uint16_t port_;
    std::string broadcast_addr_;
    uint8_t alive_msg_;
    uint8_t shutdown_msg_;
    std::string hostname_;
    bool include_hostname_;
    int sockfd_;
    struct sockaddr_in dest_;
    std::atomic<bool> running_;
    std::thread worker_;

    void run_loop();
    bool find_interface_broadcast(const std::string& if_name, std::string& out_bcast);
};

#endif // SUBNET_BROADCASTER_HPP
