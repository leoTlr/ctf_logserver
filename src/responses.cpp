#include "http_server.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using fsp = std::filesystem::path;

http::response<http::dynamic_body> HttpConnection::BadRequest(std::string const& reason) const {
    http::response<http::dynamic_body> res;
    res.result(http::status::bad_request);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) << reason << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

http::response<http::dynamic_body> HttpConnection::NotFound(boost::string_view target_user) const {
    http::response<http::dynamic_body> res;
    res.result(http::status::not_found);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) 
        << "no logs found for user " << "'" << target_user << "'" << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

http::response<http::dynamic_body> HttpConnection::ServerError(std::string const& reason) const {
    http::response<http::dynamic_body> res;
    res.result(http::status::internal_server_error);
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    beast::ostream(res.body()) << reason << std::endl;
    res.set(http::field::content_length, res.body().size());
    return res;
}

http::response<http::dynamic_body> HttpConnection::Unauthorized(std::string const& reason) const {
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
http::response<http::file_body> HttpConnection::LogfileResponse(fsp const& full_path) const {
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

// 200 OK; content_type application/jwt; body=jwtstring
http::response<http::dynamic_body> HttpConnection::PostOkResponse(std::string const& jwt) const {
    http::response<http::dynamic_body> res;
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "application/jwt;");
    beast::ostream(res.body()) << jwt << std::endl;
    res.set(http::field::content_length, jwt.size());
    return res;
}

// 200 OK with public key as base64 in body
http::response<http::string_body> HttpConnection::PubKeyResponse() const {
    http::response<http::string_body> res;
    res.set(http::field::server, server_name_);
    res.set(http::field::content_type, "text/plain");
    res.body() = pub_key_;
    res.prepare_payload();
    return res;
}