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
#include <iostream> // fail()
#include <exception>

void start_http_server(boost::asio::ip::tcp::acceptor& acceptor,
                        boost::asio::ip::tcp::socket& socket_,
                        std::filesystem::path const& logdir,
                        std::pair<std::string, std::string> const& keypair,
                        std::string const& server_name
);

void fail(std::error_code const& ec, std::string const& msg);

/*  handler class for http connections  */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {

    boost::asio::ip::tcp::socket socket_;
    boost::beast::flat_buffer readbuf_ {8192};
    boost::beast::http::request<boost::beast::http::dynamic_body> request_;

    // timer for putting a deadline on connection processing
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> deadline_ {
        socket_.get_executor().context(), 
        std::chrono::seconds(30)
    };

    std::filesystem::path const& logdir_;
    std::string const& pub_key_;
    std::string const& priv_key_;
    std::string const& server_name_;

    // asynchronously recieve a complete request message
    void readRequest();

    // decide what to respond based on http method
    void processRequest();

    // gather information for response body
    void handleGET();
    void handlePOST();

    // asynchronously write response in socket_
    template <class Body, class Fields> // has to be of type boost::beast::http::body::value_type
    void writeResponse(boost::beast::http::response<Body, Fields>&& res) {
        static_assert(boost::beast::http::is_body<Body>::value, "Body requirements not met");
        static_assert(boost::beast::http::is_fields<Fields>::value, "Fields requirements not met");

        // keep self and response alive during async write
        auto self = shared_from_this();
        auto sp = std::make_shared<boost::beast::http::response<Body, Fields>> (std::move(res));
        
        boost::beast::http::async_write(
            socket_,
            *sp,
            [self, sp] (boost::beast::error_code ec, std::size_t) {

                // end tcp conn gracefully    
                self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                self->deadline_.cancel();
            });       
    }

    // close conn after wait
    void checkDeadline();

    int getBasicAuthCredentials(std::string& auth_user, std::string& auth_pass) const;
    boost::optional<boost::string_view> extractJWT() const;
    bool verifyJWT(std::string const& token, std::string const& requested_user) const;
    int writeLogfile(std::filesystem::path const& full_path) const;

    // misc
    // construct various responses
    boost::beast::http::response<boost::beast::http::dynamic_body> BadRequest(std::string const& reason) const;
    boost::beast::http::response<boost::beast::http::dynamic_body> NotFound(boost::string_view target) const;
    boost::beast::http::response<boost::beast::http::dynamic_body> ServerError(std::string const& reason) const;
    boost::beast::http::response<boost::beast::http::dynamic_body> Unauthorized(std::string const& reason) const;
    boost::beast::http::response<boost::beast::http::file_body> LogfileResponse(std::filesystem::path const& full_path) const;
    boost::beast::http::response<boost::beast::http::dynamic_body> PostOkResponse(std::string const& jwt) const;

public:
    HttpConnection(
        boost::asio::ip::tcp::socket socket, 
        std::filesystem::path const& logdir, 
        std::pair<std::string, std::string> const& keypair,
        std::string const& server_name) :

        socket_(std::move(socket)), // take ownership of socket
        logdir_(logdir),
        pub_key_(keypair.first),
        priv_key_(keypair.second),
        server_name_(server_name)
    {   
        namespace fs = std::filesystem;
        namespace http = boost::beast::http;
        using tcp = boost::asio::ip::tcp;

        // ensure logdir exists
        std::error_code ec;
        fs::create_directory(logdir_, ec); // does nothing if exists
        if (ec) {
            fail(ec, "HttpConnection() logdir creation");
            http::write(socket_, ServerError("oops"));
            socket_.shutdown(tcp::socket::shutdown_both);
            throw std::invalid_argument(ec.message()); // dont construct obj in case of error
        }
    };

    ~HttpConnection() {
        socket_.close();
        deadline_.cancel();
    }

    void start() {
        readRequest();
        checkDeadline();
    }; 
};

#endif // HTTP_SERVER_HPP