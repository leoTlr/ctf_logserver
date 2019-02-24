/*
    took some code from https://www.boost.org/doc/libs/master/libs/beast/example/http/server/small/http_server_small.cpp
    (boost doc http server example)
*/

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <memory>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>

/*  handler class for http connections  */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {

    boost::asio::ip::tcp::socket socket;
    boost::beast::flat_buffer readbuf {8192};
    boost::beast::http::request<boost::beast::http::dynamic_body> request;
    boost::beast::http::response<boost::beast::http::dynamic_body> response;

    // timer for putting a deadline on connection processing
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> deadline {
        socket.get_executor().context(), 
        std::chrono::seconds(60)
    };

    // asynchronously recieve a complete request message
    void readRequest();

    // decide what to respond based on http method
    void processRequest();

    // gather information for response body
    void createResponse();

    // asynchronously write response in socket
    void writeResponse();

    // close conn after wait
    void checkDeadline();

public:
    HttpConnection(boost::asio::ip::tcp::socket socket) :
        socket(std::move(socket))
    {};

    void start() {
        readRequest();
        checkDeadline();
    }; 
};

void start_http_server(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::ip::tcp::socket& socket);

#endif // HTTP_SERVER_HPP