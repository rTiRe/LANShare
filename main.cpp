#include "SubnetBroadcaster.hpp"
#include "SubnetListener.hpp"
#include "MessageCodec.hpp"
#include "FileTransfer.hpp"
#include <iostream>
#include <thread>
#include <csignal>

static SubnetBroadcaster* g_broadcaster = nullptr;
static SubnetListener* g_listener = nullptr;
static FileTransfer* g_filetransfer = nullptr;

void sigint_handler(int) {
    std::cerr << "\nStopping...\n";
    // Try to send TCP shutdown to known devices first
    if (g_filetransfer && g_listener) {
        auto devices = g_listener->get_devices();
        for (const auto& [ip, info] : devices) {
            g_filetransfer->send_shutdown(ip);
        }
    }

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

    FileTransfer ft(40001);
    if (!ft.start_receiver()) {
        std::cerr << "File receiver failed to start\n";
        // continue anyway
    }

    g_broadcaster = &bc;
    g_listener = &listener;
    g_filetransfer = &ft;
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    bc.start(MessageCodec::MSG_ALIVE, MessageCodec::MSG_SHUTDOWN);

    std::cout << "Broadcasting and listening on port 40000.\n";

    // simple interactive loop: list devices and allow sending a file
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto devices = listener.get_devices();
        std::cout << "\n--- Active devices ---\n";
        for (const auto& [ip, info] : devices) {
            std::cout << ip << " (" << info.hostname << ") - last: " << MessageCodec::name_for(info.lastMessage)
                      << " [code=" << static_cast<int>(info.lastMessage) << "]" << std::endl;
        }

        std::cout << "\nEnter target IP (or empty to refresh): ";
        std::string target;
        std::getline(std::cin, target);
        if (target.empty()) continue;

        std::cout << "Enter path to file to send: ";
        std::string path;
        std::getline(std::cin, path);
        if (path.empty()) continue;

        std::cout << "Requesting transfer to " << target << "...\n";
        if (!ft.request_send(target, 40003, path)) {
            std::cout << "Request denied or timed out.\n";
            continue;
        }

        std::cout << "Request accepted — sending " << path << " to " << target << "...\n";
        if (ft.send_file(target, 40001, path)) {
            std::cout << "Send complete.\n";
        } else {
            std::cout << "Send failed.\n";
        }
    }
}
