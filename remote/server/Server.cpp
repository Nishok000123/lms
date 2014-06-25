
#include <utility>

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include "Server.hpp"

namespace Remote {
namespace Server {

Server::Server(boost::asio::io_service& ioService, const endpoint_type& bindEndpoint, boost::filesystem::path dbPath)
:
_ioService(ioService),
_acceptor(_ioService, bindEndpoint, true /*SO_REUSEADDR*/),
_connectionManager(),
_context(boost::asio::ssl::context::tlsv1_server),
_requestHandler(dbPath)
{
	_context.set_options( boost::asio::ssl::context::default_workarounds // TODO check this thing
			| boost::asio::ssl::context::single_dh_use
			| boost::asio::ssl::context::no_sslv2
			| boost::asio::ssl::context::no_sslv3
			);
//	context_.set_password_callback(boost::bind(&server::get_password, this));
	_context.use_certificate_chain_file("cert.pem"); // TODO parametrize
	_context.use_private_key_file("privkey.pem", boost::asio::ssl::context::pem); // TODO parametrize
	_context.use_tmp_dh_file("dh2048.pem");	// TODO parametrize
}

void
Server::run()
{
	// While the server is running, there is always at least one
	// asynchronous operation outstanding: the asynchronous accept call waiting
	// for new incoming connections.
	asyncAccept();
}

void
Server::asyncAccept()
{
	std::shared_ptr<Connection> newConnection = std::make_shared<Connection>(_ioService, _context, _connectionManager, _requestHandler);

	_acceptor.async_accept(newConnection->getSocket(),
			boost::bind(&Server::handleAccept, this, newConnection, boost::asio::placeholders::error));
}

void
Server::handleAccept(std::shared_ptr<Connection> newConnection, boost::system::error_code ec)
{
	std::cout << "Server::handleAccept..." << std::endl;
	// Check whether the server was stopped before this
	// completion handler had a chance to run.
	if (!_acceptor.is_open())
	{
		return;
	}

	if (!ec)
	{
		_connectionManager.start(newConnection);

		// Accept another connection
		// TODO: add some limit?
		asyncAccept();
	}
	else
		std::cerr << "handleAccept: " << ec.message() << std::endl;
}


void
Server::stop()
{
	// The server is stopped by cancelling all outstanding asynchronous
	// operations.
	_acceptor.close();
	_connectionManager.stopAll();
}

} // namespace Server
} // namespace Remote
