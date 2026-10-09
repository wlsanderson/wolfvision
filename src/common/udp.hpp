#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace common {
// IPv4 POSIX datagram transport. Payloads remain opaque to this layer.
class UdpSocket {
  public:
    UdpSocket(const std::string& address, std::uint16_t port, bool receiver);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    void send(const std::uint8_t* data, std::size_t size);
    bool receive(std::vector<std::uint8_t>& data, int timeout_ms);

  private:
    int fd_ = -1;
};
} // namespace common
