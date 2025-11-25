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

private:
    uint16_t listen_port_;
    int sockfd_;
    std::thread worker_;
    bool running_;

    void receiver_loop();
};

#endif // FILE_TRANSFER_HPP
