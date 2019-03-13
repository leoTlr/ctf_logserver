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
                        std::filesystem::path const& logdir) {
    acceptor.async_accept(socket, 
        [&](beast::error_code ec) {
            if (!ec)
                std::make_shared<HttpConnection>(std::move(socket), logdir)->start();
            start_http_server(acceptor, socket, logdir);
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
    http::response<http::dynamic_body> response;
    response.result(http::status::bad_request);
    response.set(http::field::content_type, "text/plain");
    beast::ostream(response.body()) << reason << std::endl;
    response.set(http::field::content_length, response.body().size());
    return response;
}

// return not found response
http::response<http::dynamic_body> HttpConnection::NotFound(boost::string_view target) {
    http::response<http::dynamic_body> response;
    response.result(http::status::not_found);
    response.set(http::field::content_type, "text/plain");
    target.remove_prefix(1);
    beast::ostream(response.body()) 
        << "no logs found for user " << "'" << target << "'" << std::endl;
    response.set(http::field::content_length, response.body().size());
    return response;
}

// return internal server error response
http::response<http::dynamic_body> HttpConnection::ServerError(std::string const& reason) {
    http::response<http::dynamic_body> response;
    response.result(http::status::internal_server_error);
    response.set(http::field::content_type, "text/plain");
    beast::ostream(response.body()) << reason << std::endl;
    response.set(http::field::content_length, response.body().size());
    return response;
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

    res.set(http::field::content_length, res.body().size());
    res.set(http::field::content_type, "text/plain");

    return res;
}

// gather information for response body
// GET /user1 HTTP/1.1\r\n\r\n -> send /logdir/user1.log
void HttpConnection::handleGET() {

    // todo auth

    // check if target of type /user
    if (request_.target().empty() || request_.target()[0] != '/' || 
        request_.target().find("..") != beast::string_view::npos || // prevent directory traversal
        request_.target().find('/') != request_.target().rfind('/')) // only 1 '/' allowed
    {
        return writeResponse(BadRequest("invalid user"));
    }

    // /user1 -> logdir/user1.log
    auto const user_str = request_.target().substr(1).to_string(); // string_view::substr doesnt return std::string
    auto const logfile_path = (logdir_ / fsp(user_str)).replace_extension(".log");
    
    // LogfileResponse() requires existing path
    if (!std::filesystem::exists(logfile_path))
        return writeResponse(NotFound(request_.target()));

    return writeResponse(LogfileResponse(logfile_path));
}

void HttpConnection::handlePOST() {
    // todo
    //writeResponse();
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