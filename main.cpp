#include "SubnetBroadcaster.hpp"
#include "SubnetListener.hpp"
#include <iostream>
#include <thread>
#include <csignal>

static SubnetBroadcaster* g_broadcaster = nullptr;
static SubnetListener* g_listener = nullptr;

void sigint_handler(int) {
    std::cerr << "\nStopping...\n";
    if (g_broadcaster) g_broadcaster->stop();
    if (g_listener) g_listener->stop();
    std::_Exit(0);
}

int main(int argc, char* argv[]) {
    std::string if_name;
    if (argc > 1) if_name = argv[1];

    SubnetBroadcaster bc(2000, 40000);
    if (!bc.init(if_name)) {
        std::cerr << "Init failed\n";
        return 1;
    }

    SubnetListener listener(40000);
    if (!listener.start()) {
        std::cerr << "Listener start failed\n";
        return 2;
    }

    g_broadcaster = &bc;
    g_listener = &listener;
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    bc.start("device_alive", "device_shutdown");

    std::cout << "Broadcasting and listening on port 40000.\n";

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto devices = listener.get_devices();
        std::cout << "\n--- Active devices ---\n";
        for (const auto& [ip, info] : devices) {
            std::cout << info.ip << " (" << info.hostname
                      << ") - last message: " << info.lastMessage << std::endl;
        }
    }
}
