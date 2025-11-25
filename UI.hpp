#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>
#include <memory>
#include "SubnetListener.hpp"
#include "FileTransfer.hpp"
#include "SubnetBroadcaster.hpp"

// Minimal ncurses-based UI to display devices and pending requests and allow
// accepting/rejecting requests and initiating sends.
class UI {
public:
    UI(SubnetListener& listener, FileTransfer& ft, SubnetBroadcaster& bc);
    ~UI();

    // Initialize ncurses, returns false on failure
    bool init();
    // Run the main UI loop (returns when user quits)
    void run();

private:
    SubnetListener& listener_;
    FileTransfer& ft_;
    SubnetBroadcaster& bc_;
    bool running_;

    void draw();
    void handle_input();
    void process_pending_requests();
};

#endif // UI_HPP
