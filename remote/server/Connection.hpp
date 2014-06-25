#ifndef REMOTE_CONNECTION_HPP
#define REMOTE_CONNECTION_HPP

#include <array>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "RequestHandler.hpp"

#include "messages/Header.hpp"

namespace Remote {
namespace Server {

class ConnectionManager;

/// Represents a single connection from a client.
class Connection : public std::enable_shared_from_this<Connection>
{
	public:

		typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> 	ssl_socket;

		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		typedef std::shared_ptr<Connection> pointer;

		/// Construct a connection with the given io_service.
		explicit Connection(boost::asio::io_service& ioService, boost::asio::ssl::context& context,
				ConnectionManager& manager, RequestHandler& handler);

		ssl_socket::lowest_layer_type& getSocket()	{return _socket.lowest_layer();}

		/// Start the first asynchronous operation for the connection.
		void start();

		/// Stop all asynchronous operations associated with the connection.
		void stop();

	private:
		bool _closing;

		/// Read a new message on the the connection
		void readMsg();

		/// Handle completion of ssl handshake
		void handleHandshake(const boost::system::error_code& error);

		/// Handle completion of a read operation.
		void handleReadHeader(const boost::system::error_code& e,
					std::size_t bytes_transferred);

		void handleReadMsg(const boost::system::error_code& e,
					std::size_t bytes_transferred);

		/// Socket for the connection.
		ssl_socket _socket;

		/// The manager for this connection.
		ConnectionManager& _connectionManager;

		/// The handler used to process the incoming requests.
		RequestHandler& _requestHandler;

		boost::asio::streambuf _inputStreamBuf;
		boost::asio::streambuf _outputStreamBuf;
};


} // namespace Server
} // namespace Remote

#endif


