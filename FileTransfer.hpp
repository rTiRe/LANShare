#ifndef FILE_TRANSFER_HPP
#define FILE_TRANSFER_HPP

#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <memory>
#include <atomic>

struct PendingRequest {
    std::string peer_ip;
    std::string filename;
    // -1 undecided, 0 reject, 1 accept
    std::atomic<int> decision;
    PendingRequest(const std::string& ip, const std::string& fn) : peer_ip(ip), filename(fn), decision(-1) {}
};

class FileTransfer {
public:
    FileTransfer(uint16_t listen_port = 40001);
    ~FileTransfer();

    bool start_receiver();
    void stop_receiver();

    // Blocking send of a file to remote_ip:port. Returns true on success.
    bool send_file(const std::string& remote_ip, uint16_t port, const std::string& filepath);
    // send a single-byte shutdown message via TCP to remote host
    bool send_shutdown(const std::string& remote_ip, uint16_t port = 40002);
    // request permission to send a file. Connects to control_port on remote and waits for accept.
    bool request_send(const std::string& remote_ip, uint16_t control_port, const std::string& filename, unsigned int timeout_ms = 30000);
    // polling API for incoming requests (main thread)
    std::vector<std::shared_ptr<PendingRequest>> get_pending_requests();
    // main thread calls this to decide a pending request; returns true if found and set
    bool decide_request(const std::string& peer_ip, const std::string& filename, bool accept);
    // decide by index into pending list (0-based)
    bool decide_request_by_index(size_t index, bool accept);

private:
    uint16_t listen_port_;
    int sockfd_;
    std::thread worker_;
    bool running_;
    // control server
    int control_sockfd_;
    uint16_t control_port_;
public:
    // accessors for actual ports (may differ if fallback ephemeral port was used)
    uint16_t listen_port() const { return listen_port_; }
    uint16_t control_port() const { return control_port_; }
    std::thread control_worker_;

    std::mutex pending_mutex_;
    std::vector<std::shared_ptr<PendingRequest>> pending_;
    std::condition_variable pending_cv_;

    void control_loop();

    void receiver_loop();
};

#endif // FILE_TRANSFER_HPP
