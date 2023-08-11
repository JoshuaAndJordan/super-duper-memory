#pragma once

#include "cli.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/error.hpp>

namespace net = boost::asio;
namespace beast = boost::beast;

namespace jordan {
class server_t : public std::enable_shared_from_this<server_t> {
  net::io_context &m_ioContext;
  net::ip::tcp::endpoint const m_endpoint;
  net::ip::tcp::acceptor m_acceptor;
  bool m_isOpen = false;
  command_line_interface_t const m_args;

public:
  server_t(net::io_context &context, command_line_interface_t &&args);
  void run();
  operator bool() { return m_isOpen; }

private:
  void onConnectionAccepted(beast::error_code const ec,
                            net::ip::tcp::socket socket);
  void acceptConnections();
};

} // namespace jordan
