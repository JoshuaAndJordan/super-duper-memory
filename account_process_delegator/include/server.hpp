#pragma once

#include "cli.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/error.hpp>

namespace net = boost::asio;
namespace beast = boost::beast;

namespace jordan {
class server_t : public std::enable_shared_from_this<server_t> {
  using tcp = net::ip::tcp;

  net::io_context &m_ioContext;
  tcp::acceptor m_acceptor;
  command_line_interface_t const m_args;
  bool m_isOpen = false;

public:
  server_t(net::io_context &context, command_line_interface_t &&args);
  void run();
  explicit operator bool() const { return m_isOpen; }

private:
  void onConnectionAccepted(beast::error_code ec,
                            net::ip::tcp::socket socket);
  void acceptConnections();
};

} // namespace jordan
