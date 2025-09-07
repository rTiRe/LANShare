#pragma once
#include <string>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>

class NetworkManager {
public:
    NetworkManager(int port);
    ~NetworkManager();

    void start();
    void stop();

    std::set<std::string> getKnownNodes();

private:
    void broadcaster();
    void listener();

    int port;
    std::string nodeId;  // уникальный ID текущего узла
    std::set<std::string> known_nodes;
    std::mutex nodes_mutex;

    std::thread senderThread;
    std::thread listenerThread;
    std::atomic<bool> running;
};
