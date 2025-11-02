#ifndef SUBNET_BROADCASTER_HPP
#define SUBNET_BROADCASTER_HPP

#include <string>
#include <thread>
#include <atomic>
#include <netinet/in.h>

class SubnetBroadcaster {
public:
    SubnetBroadcaster(unsigned int interval_ms = 2000, uint16_t port = 40000);
    ~SubnetBroadcaster();

    bool init(const std::string& if_name = "");
    bool start(const std::string& alive_msg = "alive", const std::string& shutdown_msg = "shutdown");
    void stop();
    bool send_now(const std::string& msg);

    std::string broadcast_address() const;
    uint16_t port() const;

private:
    unsigned int interval_ms_;
    uint16_t port_;
    std::string broadcast_addr_;
    std::string alive_msg_;
    std::string shutdown_msg_;
    int sockfd_;
    struct sockaddr_in dest_;
    std::atomic<bool> running_;
    std::thread worker_;

    void run_loop();
    bool find_interface_broadcast(const std::string& if_name, std::string& out_bcast);
};

#endif // SUBNET_BROADCASTER_HPP
