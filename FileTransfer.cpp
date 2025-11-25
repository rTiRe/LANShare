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
    if (sockfd_ < 0) {
        perror("FileTransfer: socket");
        return false;
    }

    int on = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port_);

    if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("FileTransfer: bind (preferred)");
        // try ephemeral port
        addr.sin_port = htons(0);
        if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("FileTransfer: bind (ephemeral)");
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        } else {
            // retrieve assigned port
            struct sockaddr_in actual{};
            socklen_t alen = sizeof(actual);
            if (getsockname(sockfd_, reinterpret_cast<struct sockaddr*>(&actual), &alen) == 0) {
                listen_port_ = ntohs(actual.sin_port);
            }
        }
    }

    if (listen(sockfd_, 4) < 0) {
        perror("FileTransfer: listen");
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
    int res = connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) { ::close(s); return false; }
    fd_set wf;
    struct timeval tv;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(s + 1, nullptr, &wf, nullptr, &tv) <= 0) { ::close(s); return false; }
    // check connect result
    int soerr = 0;
    socklen_t len = sizeof(soerr);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 || soerr != 0) {
        ::close(s);
        return false;
    }
    // restore flags (make socket blocking again)
    fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
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
    if (control_sockfd_ < 0) {
        perror("Control: socket");
        return;
    }
    int on = 1;
    setsockopt(control_sockfd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(control_port_);
    if (bind(control_sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("Control: bind (preferred)");
        // try ephemeral control port
        addr.sin_port = htons(0);
        if (bind(control_sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("Control: bind (ephemeral)");
            ::close(control_sockfd_);
            control_sockfd_ = -1;
            return;
        } else {
            struct sockaddr_in actual{};
            socklen_t alen = sizeof(actual);
            if (getsockname(control_sockfd_, reinterpret_cast<struct sockaddr*>(&actual), &alen) == 0) {
                control_port_ = ntohs(actual.sin_port);
            }
        }
    }
    if (listen(control_sockfd_, 4) < 0) {
        perror("Control: listen");
        ::close(control_sockfd_);
        control_sockfd_ = -1;
        return;
    }

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

            // enqueue pending request for main thread to handle
            char ipbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
            auto req = std::make_shared<PendingRequest>(std::string(ipbuf), filename);
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_.push_back(req);
            }
            pending_cv_.notify_one();

            // wait for decision with timeout (30s)
            int waited = 0;
            const int step = 100;
            while (req->decision.load() == -1 && waited < 30000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(step));
                waited += step;
            }

            uint8_t resp = MessageCodec::MSG_FILE_REJECT;
            int d = req->decision.load();
            if (d == 1) resp = MessageCodec::MSG_FILE_ACCEPT;
            // if still -1 or 0 -> reject
            send(client, &resp, sizeof(resp), 0);
        }
        ::close(client);
    }
}

std::vector<std::shared_ptr<PendingRequest>> FileTransfer::get_pending_requests() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    return pending_;
}

bool FileTransfer::decide_request(const std::string& peer_ip, const std::string& filename, bool accept) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& p : pending_) {
        if (p->peer_ip == peer_ip && p->filename == filename) {
            p->decision.store(accept ? 1 : 0);
            return true;
        }
    }
    return false;
}
