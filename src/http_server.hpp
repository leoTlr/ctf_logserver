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

inline void fail(std::error_code const& ec, std::string const& msg) {
    std::cerr << msg << ": " << ec.message() << std::endl;
}

struct query_params {
    int nr_entries = 0;
    bool debug = false;
    boost::beast::string_view new_user = "";
};

/*  handler class for http connections  */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {

    boost::asio::ip::tcp::socket socket_;
    boost::beast::flat_buffer readbuf_ {8192};
    boost::beast::http::request<boost::beast::http::string_body> request_;

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

    // extract token out of header-field
    boost::optional<boost::string_view> extractJWT() const;

    // verify token with servers RSA key
    bool verifyJWT(std::string const& token, std::string const& requested_user) const;

    // parse queries in target string
    struct query_params parseTargetQuery() const;

    // get user out of target string
    boost::string_view getTargetUser() const;

    // write response body in given file
    int writeLogfile(std::filesystem::path const& full_path) const;

    // generate a new token
    std::string newToken(std::string const& name) const;

    // construct various responses
    using http_dyn_body_res = boost::beast::http::response<boost::beast::http::dynamic_body>;
    using http_file_body_res = boost::beast::http::response<boost::beast::http::file_body>;
    using http_string_body_res = boost::beast::http::response<boost::beast::http::string_body>;
    http_dyn_body_res BadRequest(std::string const& reason) const;
    http_dyn_body_res NotFound(boost::string_view target) const;
    http_dyn_body_res ServerError(std::string const& reason) const;
    http_dyn_body_res Unauthorized(std::string const& reason) const;
    http_file_body_res LogfileResponse(std::filesystem::path const& full_path) const;
    http_dyn_body_res LastLogsResponse(std::filesystem::path const& full_path, size_t nr_lines) const;
    http_dyn_body_res tokenResponse(std::string const& jwt) const;
    http_string_body_res PubKeyResponse() const;
    http_dyn_body_res IndexResponse() const;

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

// create HttpConnection instance on incoming tcp conn
inline void start_http_server( 
    boost::asio::ip::tcp::acceptor& acceptor,
    boost::asio::ip::tcp::socket& socket,
    std::filesystem::path const& logdir,
    std::pair<std::string, std::string> const& keypair,
    std::string const& server_name) 
{
    acceptor.async_accept(
        socket, 
        [&](boost::beast::error_code ec) {
            if (!ec) {
                try {
                    std::make_shared<HttpConnection>(std::move(socket), logdir, keypair, server_name)->start();
                } catch (std::invalid_argument& e) {
                    std::cerr << "[ERROR] " << e.what() << std::endl;
                }
            }
            start_http_server(acceptor, socket, logdir, keypair, server_name);
        });
}

#endif // HTTP_SERVER_HPP