#include <boost/beast/core/detail/base64.hpp> // caution: could move somwhere else
#include <exception> // std::out_of_range for http::basic_fields::at
#include <fstream>

#include "http_server.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using fsp = std::filesystem::path;

void fail(std::error_code const& ec, std::string const& msg) {
    std::cerr << msg << ": " << ec.message() << std:: endl;
}

void start_http_server(tcp::acceptor& acceptor, 
                        tcp::socket& socket, 
                        std::filesystem::path const& logdir,
                        std::string const& server_name) {
    acceptor.async_accept(socket, 
        [&](beast::error_code ec) {
            if (!ec)
                std::make_shared<HttpConnection>(std::move(socket), logdir, server_name)->start();
            start_http_server(acceptor, socket, logdir, server_name);
        });
}

// asynchonously recieve complete request message
void HttpConnection::readRequest() {
    auto self = shared_from_this();

    http::async_read(
        socket_,
        readbuf_,
        request_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {

            boost::ignore_unused(bytes_transferred);
            if (!ec)
                self->processRequest();
            else {
                if (ec.message().substr(0, 3) == "bad") // bad [ target|version|... ]
                    self->writeResponse(self->BadRequest(ec.message()));
                else {
                    fail(ec, "readRequest()");
                    self->writeResponse(self->ServerError(ec.message()));
                }
            }
        });
}

// decide what to respond based on http method
void HttpConnection::processRequest() {
    // no auth here -> in handle()

    switch (request_.method()) {
        case http::verb::get: // respond with logentries for requested user
            return handleGET(); // gather contents for body
        case http::verb::post: // add provided logentries for user
            // todo: auth
            return handlePOST();
        case http::verb::head: // (maybe last logfile update timepoint for  req. user)
            // falltrhugh until implemented
        default:
            return writeResponse(BadRequest("invalid request method"));
    }
}

// return bad request response
http::response<http::dynamic_body> HttpConnection::BadRequest(std::string const& reason) {
    http::response<http::dynamic_body> res;
    res.result(http::status::bad_request);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) << reason << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

// return not found response
http::response<http::dynamic_body> HttpConnection::NotFound(boost::string_view target) {
    http::response<http::dynamic_body> res;
    res.result(http::status::not_found);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    target.remove_prefix(1);
    beast::ostream(res.body()) 
        << "no logs found for user " << "'" << target << "'" << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

// return internal server error response
http::response<http::dynamic_body> HttpConnection::ServerError(std::string const& reason) {
    http::response<http::dynamic_body> res;
    res.result(http::status::internal_server_error);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) << reason << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

http::response<http::dynamic_body> HttpConnection::Unauthorized(std::string const& reason) {
    http::response<http::dynamic_body> res;
    res.result(http::status::unauthorized);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) << reason << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

// return response with file specified in full_path as body
// path has to be valid
http::response<http::file_body> HttpConnection::LogfileResponse(fsp const& full_path) {
    assert(std::filesystem::exists(full_path));

    // construct response with logfile as body
    beast::error_code ec;
    http::response<http::file_body> res {http::status::ok, request_.version()};
    res.body().open(full_path.c_str(), beast::file_mode::scan, ec);

    if (ec)
        fail(ec, "LogfileResponse()");

    res.set(http::field::server, server_name_);
    res.set(http::field::content_length, res.body().size());
    res.set(http::field::content_type, "text/plain");

    return res;
}

// gather information for response body
// GET /user1 HTTP/1.1\r\n\r\n -> send /logdir/user1.log
void HttpConnection::handleGET() {

    // todo auth with JWT

    // check if target of type /user
    if (request_.target().empty() || request_.target()[0] != '/' || 
        request_.target().find("..") != beast::string_view::npos || // prevent directory traversal
        request_.target().find('/') != request_.target().rfind('/')) // only 1 '/' allowed
    {
        return writeResponse(BadRequest("invalid request target"));
    }

    // /user1 -> logdir/user1.log
    auto const target_user = request_.target().substr(1).to_string(); // string_view::substr doesnt return std::string
    auto const logfile_path = (logdir_ / fsp(target_user)).replace_extension(".log");

    // http basic auth (field authorization with value "Basic "+base64(user:pw))
    // only continue if credentials provided
    std::string auth_user;
    std::string auth_pass;
    switch (getBasicAuthCredentials(auth_user, auth_pass)) {
        case 0: // success, just continue
            break;
        case -1: 
            return writeResponse(Unauthorized("no authentification method provided"));
        case -2: // todo: accept and handle JWT
            return writeResponse(BadRequest("bad authentication method"));
        case -3:
            return writeResponse(BadRequest("authentication credentials malformed"));
        default: assert(false); // should not happen
    }

    // todo: match pw (with?)
    if (auth_user != target_user) {
        return writeResponse(Unauthorized("provided user has no permission for requested logfile"));
    }

    // LogfileResponse() requires existing path
    if (!std::filesystem::exists(logfile_path))
        return writeResponse(NotFound(request_.target()));

    // construct and send response containing requested logfile
    return writeResponse(LogfileResponse(logfile_path));
}

/*  http basic auth (field authorization with value "Basic "+base64(user:pw))
    returns 0 and fills auth_user and auth_pass if successful
    -1 Header field Authentification not present
    -2 auth method != Basic
    -3 auth credentials malformed    */
int HttpConnection::getBasicAuthCredentials(std::string& auth_user, std::string& auth_pass) {

    try {
        auto b64_credentials = request_.at(http::field::authorization);
        if (b64_credentials.find("Basic ") || b64_credentials.find("basic "))
            b64_credentials.remove_prefix(6);
        else return -2; // auth method != Basic

        // WARNING beast::detail namespace considered private -> could move somwhere else or disappear
        auto str_credentials = beast::detail::base64_decode(b64_credentials.to_string());

        std::size_t colon_pos = str_credentials.find(':');
        if (colon_pos != std::string::npos) {
            auth_user = str_credentials.substr(0, colon_pos);
            auth_pass = str_credentials.substr(colon_pos+1);
        } else return -3; // auth credentials malformed

    } catch (std::out_of_range& e) {
        return -1; // no Authentification field present
    }

    return 0;
}

void HttpConnection::handlePOST() {
    // todo auth

    // todo: write encrypted
    
    if (request_.body().size() < 1)
        return writeResponse(BadRequest("empty message"));
}

// close conn after wait
void HttpConnection::checkDeadline() {
    auto self = shared_from_this();

    deadline_.async_wait(
        [self] (beast::error_code ec) {
            if (!ec)
                self->socket_.close(ec);
        });
}