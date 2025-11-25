#include "FileTransfer.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/stat.h>
#include <chrono>
#include "MessageCodec.hpp"

FileTransfer::FileTransfer(uint16_t listen_port)
    : listen_port_(listen_port), sockfd_(-1), running_(false), control_sockfd_(-1), control_port_(40003) {}

FileTransfer::~FileTransfer() {
    stop_receiver();
}

bool FileTransfer::start_receiver() {
    if (running_) return true;
    sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) return false;

    int on = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port_);

    if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    if (listen(sockfd_, 4) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    running_ = true;
    worker_ = std::thread(&FileTransfer::receiver_loop, this);
    // start control server
    control_worker_ = std::thread(&FileTransfer::control_loop, this);
    return true;
}

void FileTransfer::stop_receiver() {
    if (!running_) return;
    running_ = false;
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
        ::close(sockfd_);
        sockfd_ = -1;
    }
    if (worker_.joinable()) worker_.join();
    if (control_sockfd_ >= 0) {
        ::shutdown(control_sockfd_, SHUT_RDWR);
        ::close(control_sockfd_);
        control_sockfd_ = -1;
    }
    if (control_worker_.joinable()) control_worker_.join();
}

bool FileTransfer::request_send(const std::string& remote_ip, uint16_t control_port, const std::string& filename, unsigned int timeout_ms) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(control_port);
    if (inet_pton(AF_INET, remote_ip.c_str(), &addr.sin_addr) != 1) { ::close(s); return false; }
    // set connect timeout via non-blocking connect
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    fd_set wf;
    struct timeval tv;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(s + 1, nullptr, &wf, nullptr, &tv) <= 0) { ::close(s); return false; }
    // send request: code + filename length + filename
    uint8_t code = MessageCodec::MSG_FILE_REQUEST;
    if (send(s, &code, sizeof(code), 0) != sizeof(code)) { ::close(s); return false; }
    uint16_t name_len = filename.size();
    uint16_t name_len_be = htons(name_len);
    if (send(s, &name_len_be, sizeof(name_len_be), 0) != sizeof(name_len_be)) { ::close(s); return false; }
    if (send(s, filename.data(), name_len, 0) != (ssize_t)name_len) { ::close(s); return false; }
    // wait for accept
    uint8_t resp;
    ssize_t r = recv(s, &resp, sizeof(resp), 0);
    ::close(s);
    return r == sizeof(resp) && resp == MessageCodec::MSG_FILE_ACCEPT;
}

bool FileTransfer::send_file(const std::string& remote_ip, uint16_t port, const std::string& filepath) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, remote_ip.c_str(), &addr.sin_addr) != 1) {
        ::close(s);
        return false;
    }

    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s);
        return false;
    }

    std::ifstream in(filepath, std::ios::binary);
    if (!in) { ::close(s); return false; }

    // send filename length + filename + file size (8 bytes) + data
    std::string filename;
    auto pos = filepath.find_last_of("/\\");
    filename = (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);

    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) { ::close(s); return false; }
    uint64_t fsize = st.st_size;

    uint16_t name_len = filename.size();
    uint16_t name_len_be = htons(name_len);
    if (send(s, &name_len_be, sizeof(name_len_be), 0) != sizeof(name_len_be)) { ::close(s); return false; }
    if (send(s, filename.data(), name_len, 0) != (ssize_t)name_len) { ::close(s); return false; }

    uint64_t fsize_be = htobe64(fsize);
    if (send(s, &fsize_be, sizeof(fsize_be), 0) != sizeof(fsize_be)) { ::close(s); return false; }

    char buf[4096];
    while (in) {
        in.read(buf, sizeof(buf));
        std::streamsize r = in.gcount();
        if (r <= 0) break;
        if (send(s, buf, r, 0) != r) { ::close(s); return false; }
    }

    ::close(s);
    return true;
}

bool FileTransfer::send_shutdown(const std::string& remote_ip, uint16_t port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, remote_ip.c_str(), &addr.sin_addr) != 1) {
        ::close(s);
        return false;
    }

    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s);
        return false;
    }

    uint8_t code = MessageCodec::MSG_SHUTDOWN;
    ssize_t r = send(s, &code, sizeof(code), 0);
    ::close(s);
    return r == (ssize_t)sizeof(code);
}

void FileTransfer::receiver_loop() {
    mkdir("recv", 0755);
    while (running_) {
        struct sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int client = accept(sockfd_, reinterpret_cast<struct sockaddr*>(&peer), &plen);
        if (client < 0) {
            if (!running_) break;
            continue;
        }

        // read filename length
        uint16_t name_len_be;
        if (recv(client, &name_len_be, sizeof(name_len_be), MSG_WAITALL) != sizeof(name_len_be)) { ::close(client); continue; }
        uint16_t name_len = ntohs(name_len_be);
        std::string filename(name_len, '\0');
        if (recv(client, &filename[0], name_len, MSG_WAITALL) != (ssize_t)name_len) { ::close(client); continue; }

        uint64_t fsize_be;
        if (recv(client, &fsize_be, sizeof(fsize_be), MSG_WAITALL) != sizeof(fsize_be)) { ::close(client); continue; }
        uint64_t fsize = be64toh(fsize_be);

        std::string outpath = std::string("recv/") + filename;
        std::ofstream out(outpath, std::ios::binary);
        uint64_t remaining = fsize;
        char buf[4096];
        while (remaining > 0) {
            ssize_t r = recv(client, buf, sizeof(buf), 0);
            if (r <= 0) break;
            out.write(buf, r);
            remaining -= r;
        }
        out.close();
        ::close(client);
    }
}

void FileTransfer::control_loop() {
    control_sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (control_sockfd_ < 0) return;
    int on = 1;
    setsockopt(control_sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(control_port_);
    if (bind(control_sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(control_sockfd_);
        control_sockfd_ = -1;
        return;
    }
    if (listen(control_sockfd_, 4) < 0) { ::close(control_sockfd_); control_sockfd_ = -1; return; }

    while (running_) {
        struct sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int client = accept(control_sockfd_, reinterpret_cast<struct sockaddr*>(&peer), &plen);
        if (client < 0) { if (!running_) break; continue; }
        uint8_t code;
        if (recv(client, &code, sizeof(code), MSG_WAITALL) != sizeof(code)) { ::close(client); continue; }
        if (code == MessageCodec::MSG_FILE_REQUEST) {
            uint16_t name_len_be;
            if (recv(client, &name_len_be, sizeof(name_len_be), MSG_WAITALL) != sizeof(name_len_be)) { ::close(client); continue; }
            uint16_t name_len = ntohs(name_len_be);
            std::string filename(name_len, '\0');
            if (recv(client, &filename[0], name_len, MSG_WAITALL) != (ssize_t)name_len) { ::close(client); continue; }

            bool accept = true;
            // Try to prompt the local user via /dev/tty
            FILE* tty = fopen("/dev/tty", "r+");
            if (tty) {
                fprintf(tty, "Incoming file request from %s: %s. Accept? [y/N]: ", inet_ntoa(peer.sin_addr), filename.c_str());
                fflush(tty);
                int ch = fgetc(tty);
                if (ch != 'y' && ch != 'Y') accept = false;
                fclose(tty);
            } else {
                // no tty available, default to auto-accept
                accept = true;
            }

            uint8_t resp = accept ? MessageCodec::MSG_FILE_ACCEPT : MessageCodec::MSG_FILE_REJECT;
            send(client, &resp, sizeof(resp), 0);
        }
        ::close(client);
    }
}
