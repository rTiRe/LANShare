#ifndef FILE_TRANSFER_HPP
#define FILE_TRANSFER_HPP

#include <string>
#include <thread>

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
    bool request_send(const std::string& remote_ip, uint16_t control_port, const std::string& filename, unsigned int timeout_ms = 3000);

private:
    uint16_t listen_port_;
    int sockfd_;
    std::thread worker_;
    bool running_;
    // control server
    int control_sockfd_;
    uint16_t control_port_;
    std::thread control_worker_;

    void control_loop();

    void receiver_loop();
};

#endif // FILE_TRANSFER_HPP
