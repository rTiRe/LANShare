#include "NetworkManager.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdlib>

constexpr int BROADCAST_INTERVAL = 3;

NetworkManager::NetworkManager(int port) : port(port), running(false) {
    // Генерируем уникальный ID узла (PID + random)
    nodeId = "NODE_" + std::to_string(getpid()) + "_" + std::to_string(rand());
    broadcastAddress = "192.168.1.255";
}

NetworkManager::~NetworkManager() {
    stop();
}

void NetworkManager::start() {
    running = true;
    senderThread = std::thread(&NetworkManager::broadcaster, this);
    listenerThread = std::thread(&NetworkManager::listener, this);
}

void NetworkManager::stop() {
    running = false;
    if (senderThread.joinable()) senderThread.join();
    if (listenerThread.joinable()) listenerThread.join();
}

std::set<std::string> NetworkManager::getKnownNodes() {
    std::lock_guard<std::mutex> lock(nodes_mutex);
    return known_nodes;
}

void NetworkManager::broadcaster() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(broadcastAddress.c_str());

    while (running) {
        std::string msg = "ALIVE:" + nodeId;
        sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
        std::this_thread::sleep_for(std::chrono::seconds(BROADCAST_INTERVAL));
    }

    close(sock);
}

void NetworkManager::listener() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return;
    }

    char buffer[1024];
    while (running) {
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&sender, &sender_len);
        if (len > 0) {
            buffer[len] = '\0';
            std::string msg(buffer);

            if (msg.rfind("ALIVE:", 0) == 0) {
                std::string senderId = msg.substr(6);
                if (senderId == nodeId) {
                    // это наш пакет → игнорируем
                    continue;
                }

                std::string ip = inet_ntoa(sender.sin_addr);

                std::lock_guard<std::mutex> lock(nodes_mutex);
                if (known_nodes.insert(ip).second) {
                    std::cout << "[INFO] Новый узел: " << ip << " (id=" << senderId << ")" << std::endl;
                }
            }
        }
    }

    close(sock);
}
