#include "common/udp.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace common {
namespace {
std::runtime_error failure(const char* operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}
} // namespace
UdpSocket::UdpSocket(const std::string& address, std::uint16_t port, bool receiver) {
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1)
        throw std::runtime_error("address must be a numeric IPv4 address");
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0)
        throw failure("socket");
    const int result =
        receiver ? bind(fd_, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint))
                 : connect(fd_, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
    if (result < 0) {
        const auto error = failure(receiver ? "bind" : "connect");
        close(fd_);
        fd_ = -1;
        throw error;
    }
}
UdpSocket::~UdpSocket() {
    if (fd_ >= 0)
        close(fd_);
}
void UdpSocket::send(const std::uint8_t* data, std::size_t size) {
    ssize_t sent;
    do {
        sent = ::send(fd_, data, size, 0);
    } while (sent < 0 && errno == EINTR);
    if (sent < 0)
        throw failure("send");
    if (static_cast<std::size_t>(sent) != size)
        throw std::runtime_error("incomplete UDP send");
}
bool UdpSocket::receive(std::vector<std::uint8_t>& data, int timeout_ms) {
    pollfd descriptor{fd_, POLLIN, 0};
    int ready;
    do {
        ready = poll(&descriptor, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0)
        throw failure("poll");
    if (ready == 0)
        return false;
    if (!(descriptor.revents & POLLIN))
        throw std::runtime_error("UDP socket poll error");
    data.resize(65536); // Larger than every possible IPv4 UDP payload; no silent truncation.
    ssize_t received;
    do {
        received = recv(fd_, data.data(), data.size(), 0);
    } while (received < 0 && errno == EINTR);
    if (received < 0)
        throw failure("recv");
    data.resize(static_cast<std::size_t>(received));
    return true;
}
} // namespace common
