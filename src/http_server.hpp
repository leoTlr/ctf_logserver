/*
    took some code from https://www.boost.org/doc/libs/master/libs/beast/example/http/server/small/http_server_small.cpp
    (boost doc http server example)
*/

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <memory>
#include <chrono>
#include <string>
#include <filesystem>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>

    #include <iostream>

#include "logtypes.hpp"

/*  handler class for http connections  */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {

    boost::asio::ip::tcp::socket socket;
    boost::beast::flat_buffer readbuf {8192};
    boost::beast::http::request<boost::beast::http::dynamic_body> request;

    // timer for putting a deadline on connection processing
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> deadline {
        socket.get_executor().context(), 
        std::chrono::seconds(60)
    };

    std::shared_ptr<std::filesystem::path const> logdir;

    // asynchronously recieve a complete request message
    void readRequest();

    // decide what to respond based on http method
    void processRequest();

    // gather information for response body
    void handleGET();
    void handlePOST();

    // asynchronously write response in socket
    template <class Body> // has to be of type boost::beast::http::body
    void writeResponse(boost::beast::http::response<Body> response) {
        std::cout << "writeResponse start" << std::endl; std::cout.flush();

        auto self = shared_from_this();
        response.set(boost::beast::http::field::content_length, response.body().size());

        std::cout << "writeResponse be4 async_write" << std::endl; std::cout.flush();
        boost::beast::http::async_write(
            socket,
            response,
            [self] (boost::beast::error_code ec, std::size_t bytes_transferred) {
                std::cout << "writeResponse compl handler start" << std::endl; std::cout.flush();
                // todo: completion handler does not get invoked

                boost::ignore_unused(bytes_transferred);
                if (ec)
                    std::cerr << ec.message() << std::endl;

                // end tcp conn gracefully    
                self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                self->deadline.cancel();

                std::cout << "writeResponse compl handler fin" << std::endl; std::cout.flush();
            });

        std::cout << "writeResponse fin" << std::endl; std::cout.flush();        
    }

    // close conn after wait
    void checkDeadline();

    // misc
    // send error responses
    void sendBadRequest(std::string const& reason);
    void sendNotFound();
    void sendServerError(std::string const& reason);

public:
    HttpConnection(boost::asio::ip::tcp::socket socket, std::shared_ptr<std::filesystem::path const> const& logdir) :
        socket(std::move(socket)),
        logdir(logdir)
    {   
        // ensure logdir exists
        std::error_code ec;
        std::filesystem::create_directory(*logdir, ec); // does nothing if exists
        if (ec) {
            std::cerr << ec.message() << std:: endl;
            throw; // dont construct obj in case of error
        }
    };

    ~HttpConnection() {
        socket.close();
        deadline.cancel();
    }

    void start() {
        readRequest();
        checkDeadline();
    }; 
};

void start_http_server(boost::asio::ip::tcp::acceptor& acceptor,
                        boost::asio::ip::tcp::socket& socket,
                        std::shared_ptr<std::filesystem::path const> const& logdir);

#endif // HTTP_SERVER_HPP