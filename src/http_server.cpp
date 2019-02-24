

#include "http_server.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


void start_http_server(tcp::acceptor& acceptor, tcp::socket& socket) {
    acceptor.async_accept(socket, 
        [&](beast::error_code ec) {
            if (!ec)
                std::make_shared<HttpConnection>(std::move(socket))->start();
            start_http_server(acceptor, socket);
        });
}

// asynchonously recieve complete request message
void HttpConnection::readRequest() {
    auto self = shared_from_this();

    http::async_read(
        socket,
        readbuf,
        request,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            boost::ignore_unused(bytes_transferred);
            if (!ec)
                self->processRequest();
        });
}

// decide what to respond based on http method
void HttpConnection::processRequest() {
    // 1.1 default
    // no auth here -> in createResponse()
    response.keep_alive(false);
    response.set(http::field::server, "logserver v0.1");

    switch (request.method()) {
        case http::verb::get: // respond with logentries for requested user
            response.result(http::status::ok);
            createResponse(); // gather contents for body
            break;
        case http::verb::post: // add provided logentries for user
            // todo: auth
            response.result(http::status::ok);
            createResponse();
            break;
        case http::verb::head: // (maybe last logfile update timepoint for  req. user)
            // falltrhugh until implemented
        default:
            response.result(http::status::bad_request);
            response.set(http::field::content_type, "text/plain");
            beast::ostream(response.body())
                << "Invalid request-method '"
                << std::string(request.method_string())
                << "'";
            break;
    }
    writeResponse();
}

// gather information for response body
void HttpConnection::createResponse() {
    // todo
}

// asynchronously write response
void HttpConnection::writeResponse() {
    auto self = shared_from_this();
    response.set(http::field::content_length, response.body().size());

    http::async_write(
        socket,
        response,
        [self] (beast::error_code ec, std::size_t) {
            self->socket.shutdown(tcp::socket::shutdown_send, ec);
            self->deadline.cancel();
        });
}

// close conn after wait
void HttpConnection::checkDeadline() {
    auto self = shared_from_this();

    deadline.async_wait(
        [self] (beast::error_code ec) {
            if (!ec)
                self->socket.close(ec);
        });
}