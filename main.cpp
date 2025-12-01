#include "SubnetBroadcaster.hpp"
#include "SubnetListener.hpp"
#include "MessageCodec.hpp"
#include "FileTransfer.hpp"
#include "UI.hpp"
#include "UIQt.hpp"
#include <QApplication>
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

static SubnetBroadcaster* g_broadcaster = nullptr;
static SubnetListener* g_listener = nullptr;
static FileTransfer* g_filetransfer = nullptr;
static std::atomic<bool> g_terminate(false);

// Signal handler sets a flag only (async-signal-safe)
void sigint_handler(int) {
    g_terminate.store(true);
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

    std::cout << "Broadcasting and listening on port 40000. Starting UI...\n";
    // Try Qt UI first
    bool qtStarted = false;
    try {
        QApplication app(argc, argv);
        qtStarted = true;
        UIQt w(listener, ft, bc);
        w.show();
        app.exec();
    } catch (...) {
        qtStarted = false;
    }
    if (!qtStarted) {
        // fallback to ncurses UI
        UI ui(listener, ft, bc);
        if (!ui.init()) {
            std::cerr << "UI init failed, falling back to console mode\n";
        } else {
            ui.run();
        }
    }

    // if termination requested, attempt to notify peers
    if (g_terminate.load()) {
        std::cerr << "\nStopping... sending shutdown to peers\n";
        if (g_filetransfer && g_listener) {
            auto devices = g_listener->get_devices();
            for (const auto& [ip, info] : devices) {
                g_filetransfer->send_shutdown(ip);
            }
        }
    }

    // clean shutdown
    if (g_broadcaster) g_broadcaster->stop();
    if (g_listener) g_listener->stop();
    if (g_filetransfer) g_filetransfer->stop_receiver();
    return 0;
}
