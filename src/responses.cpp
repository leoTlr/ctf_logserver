#include <fstream>
#include <vector>
#include "http_server.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace fs = std::filesystem;

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
http::response<http::file_body> HttpConnection::LogfileResponse(fs::path const& full_path) const {
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

// return res with last nr_lines of logfile (same as tail -n nr_lines would do)
// path has to be valid
// file must be ASCII-encoded (maybe utf-8, but no utf-16)
http::response<http::dynamic_body> HttpConnection::LastLogsResponse(fs::path const& full_path, size_t nr_lines) const {
    assert(std::filesystem::exists(full_path));

    http::response<http::dynamic_body> res {http::status::ok, request_.version()};

    std::ifstream log_file {full_path};
    if (!log_file) {
        std::cerr << "LastLogsResponse() ifstream open" << std::endl;
        return ServerError("could not open logfile");
    }

    // search backwards counting newlines and saving positions
    std::vector<size_t> line_start_positions;
    log_file.seekg(-2, log_file.end); // dont care about last char
    size_t newline_counter = 0;
    char c;

    while (newline_counter < nr_lines && log_file.good()) {

        log_file.get(c);
        if (c != '\n') {
            log_file.seekg(-2, log_file.cur);
            continue;
        }

        log_file.unget();
        log_file.seekg(1, log_file.cur);
        line_start_positions.push_back(log_file.tellg());
        log_file.seekg(-2, log_file.cur);

        newline_counter++;
    } 
    
    // if there are less lines than requested, just get whole file
    if (newline_counter < nr_lines) {
        log_file.clear();
        log_file.seekg(0, log_file.beg);

        beast::ostream(res.body()) << log_file.rdbuf();
    }
    else { // copy only last nr_lines requested lines
        std::string line;
        for (auto pos = line_start_positions.crbegin(); pos != line_start_positions.crend(); pos++) {
            log_file.seekg(*pos);
            std::getline(log_file, line);
            beast::ostream(res.body()) << line << std::endl;
        }
    }

    res.set(http::field::server, server_name_);
    res.set(http::field::content_length, res.body().size());
    res.set(http::field::content_type, "text/plain");

    return res;
}


// 501 not implemented in case of target == /index.html
// return static page indicating that there is no frontend
// path has to be valid
http::response<http::dynamic_body> HttpConnection::IndexResponse() const {
    http::response<http::dynamic_body> res;
    res.result(http::status::not_implemented);
    res.set(http::field::content_type, "text/html");

    auto body = beast::ostream(res.body());

    body
        << "<!DOCTYPE html>\n"
        << "<html>"
        << "<head> " << "<title>oops</title> " << "</head>\n"
        << "<body> " << "<p> There is no frontend. Read the api-reference for usage information </p> "
        << "</body>\n" << "</html>\n" << std::endl;

    return res;
}

// 200 OK; content_type application/jwt; body=jwtstring
http::response<http::dynamic_body> HttpConnection::tokenResponse(std::string const& jwt) const {
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