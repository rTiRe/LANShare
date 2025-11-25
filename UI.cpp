#include "UI.hpp"
#include <ncurses.h>
#include <chrono>
#include <thread>
#include <iostream>

UI::UI(SubnetListener& listener, FileTransfer& ft, SubnetBroadcaster& bc)
    : listener_(listener), ft_(ft), bc_(bc), running_(false) {}

UI::~UI() {
    endwin();
}

bool UI::init() {
    if (initscr() == nullptr) return false;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // non-blocking getch
    curs_set(0);
    return true;
}

void UI::draw() {
    clear();
    mvprintw(0, 0, "LANShare - devices (press q to quit, s to send file)");

    auto devices = listener_.get_devices();
    int row = 2;
    mvprintw(1, 0, "%-16s  %-20s  %s", "IP", "Hostname", "Status");
    for (const auto& [ip, info] : devices) {
        mvprintw(row++, 0, "%-16s  %-20s  %s", ip.c_str(), info.hostname.c_str(), MessageCodec::name_for(info.lastMessage).c_str());
    }

    mvprintw(row + 1, 0, "Pending file requests:");
    auto pending = ft_.get_pending_requests();
    int prow = row + 2;
    for (size_t i = 0; i < pending.size(); ++i) {
        auto& p = pending[i];
        const char* state = (p->decision.load() == -1) ? "awaiting" : (p->decision.load() == 1 ? "accepted" : "rejected");
        mvprintw(prow + i, 0, "%2zu) %s  %s  [%s]", i + 1, p->peer_ip.c_str(), p->filename.c_str(), state);
    }

    mvprintw(LINES - 2, 0, "Commands: q=quit, s=send file, 1/2/... accept pending request");
    refresh();
}

void UI::process_pending_requests() {
    auto pending = ft_.get_pending_requests();
    for (size_t i = 0; i < pending.size(); ++i) {
        auto& p = pending[i];
        // if decision already set, skip
        if (p->decision.load() != -1) continue;
        // no automatic action here; UI will let user press numeric keys
    }
}

void UI::handle_input() {
    int ch = getch();
    if (ch == ERR) return;
    if (ch == 'q' || ch == 'Q') {
        running_ = false;
        return;
    }
    if (ch == 's' || ch == 'S') {
        // prompt for target and path in blocking mode: temporarily enable echo and blocking
        nodelay(stdscr, FALSE);
        echo();
        curs_set(1);
        char ipbuf[64];
        char pathbuf[256];
        mvprintw(LINES - 4, 0, "Enter target IP: ");
        getnstr(ipbuf, sizeof(ipbuf) - 1);
        mvprintw(LINES - 3, 0, "Enter path to file: ");
        getnstr(pathbuf, sizeof(pathbuf) - 1);
        noecho();
        curs_set(0);
        nodelay(stdscr, TRUE);
        std::string ip = ipbuf;
        std::string path = pathbuf;
        if (!ip.empty() && !path.empty()) {
            uint16_t ctrl = ft_.control_port();
            mvprintw(LINES - 5, 0, "Requesting transfer to %s (control port %u)...", ip.c_str(), ctrl);
            refresh();
            bool ok = ft_.request_send(ip, ctrl, path, 30000);
            if (!ok) {
                mvprintw(LINES - 5, 0, "Request denied or timed out.                         ");
            } else {
                mvprintw(LINES - 5, 0, "Request accepted — sending...                       ");
                refresh();
                bool sent = ft_.send_file(ip, ft_.listen_port(), path);
                if (sent) mvprintw(LINES - 5, 0, "Send complete.                                     ");
                else mvprintw(LINES - 5, 0, "Send failed.                                       ");
            }
            refresh();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        return;
    }

    // numeric keys to accept pending requests (1-based index)
    if (ch >= '1' && ch <= '9') {
        int idx = ch - '1';
        auto pending = ft_.get_pending_requests();
        if ((size_t)idx < pending.size()) {
            auto p = pending[idx];
            ft_.decide_request(p->peer_ip, p->filename, true);
        }
    }
}

void UI::run() {
    running_ = true;
    while (running_) {
        draw();
        process_pending_requests();
        handle_input();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
