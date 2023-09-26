#include "server.hpp"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <spdlog/spdlog.h>

namespace jordan {

server_t::server_t(net::io_context &context, command_line_interface_t &&args)
    : m_ioContext(context),
      m_acceptor(net::make_strand(m_ioContext)), m_args(std::move(args)) {
  beast::error_code ec{}; // used when we don't need to throw all around
  tcp::endpoint endpoint(net::ip::make_address(args.ip_address), args.port);
  m_acceptor.open(endpoint.protocol(), ec);
  if (ec) {
    spdlog::error("Could not open socket: {}", ec.message());
    return;
  }

  m_acceptor.set_option(net::socket_base::reuse_address(true), ec);
  if (ec) {
    spdlog::error("set_option failed: {}", ec.message());
    return;
  }

  m_acceptor.bind(endpoint, ec);
  if (ec) {
    spdlog::error("binding failed: {}", ec.message());
    return;
  }

  m_acceptor.listen(net::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("not able to listen: {}", ec.message());
    return;
  }

  m_isOpen = true;
}

void server_t::run() {
  if (m_isOpen)
    return acceptConnections();
}

void server_t::onConnectionAccepted(beast::error_code const ec,
                                    net::ip::tcp::socket socket) {
  if (ec)
    return spdlog::error("error on connection: {}", ec.message());

  std::make_shared<session_t>(m_ioContext, std::move(socket))
      ->add_endpoint_interfaces()
      ->run();
  acceptConnections();
}

void server_t::acceptConnections() {
  m_acceptor.async_accept(
      net::make_strand(m_ioContext),
      [self = shared_from_this()](beast::error_code const ec, net::ip::tcp::socket socket) {
        return self->onConnectionAccepted(ec, std::move(socket));
      });
}

} // namespace jordan
