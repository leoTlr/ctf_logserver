
    #include <fstream>
    #include <iostream>

#include "http_server.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using fsp = std::filesystem::path;


void start_http_server(tcp::acceptor& acceptor, 
                        tcp::socket& socket, 
                        std::shared_ptr<std::filesystem::path const> const& logdir) {
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
        socket,
        readbuf,
        request,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            std::cout << "readRequest async_read compl handler start" << std::endl; std::cout.flush();

            boost::ignore_unused(bytes_transferred);
            if (!ec)
                self->processRequest();
            else
                std::cerr << ec.message() << std::endl;
            
            std::cout << "readRequest async_read compl handler fin" << std::endl; std::cout.flush();            
        });
}

// decide what to respond based on http method
void HttpConnection::processRequest() {
    // 1.1 default
    // no auth here -> in handle()

    switch (request.method()) {
        case http::verb::get: // respond with logentries for requested user
            //response.result(http::status::ok);
            handleGET(); // gather contents for body (calls writeResponse())
            break;
        case http::verb::post: // add provided logentries for user
            // todo: auth
            //response.result(http::status::ok);
            handlePOST(); // calls writeRespone()
            break;
        case http::verb::head: // (maybe last logfile update timepoint for  req. user)
            // falltrhugh until implemented
        default:
            sendBadRequest("invalid request method"); // calls writeRespone()
            break;
    }
}

// set response to bad request
void HttpConnection::sendBadRequest(std::string const& reason) {
    http::response<http::dynamic_body> response;
    response.result(http::status::bad_request);
    response.set(http::field::content_type, "text/plain");
    beast::ostream(response.body()) << reason;
    writeResponse(response);
}

// set response to file not found and call writeResponse()
void HttpConnection::sendNotFound() {
    http::response<http::dynamic_body> response;
    response.result(http::status::not_found);
    response.set(http::field::content_type, "text/plain");
    beast::ostream(response.body()) 
        << "no logs found for user " << "'" 
        << request.target().substr(request.target().front()+1)
        << "'";
    writeResponse(response);
}

// set response to internal server error
void HttpConnection::sendServerError(std::string const& reason) {
    http::response<http::dynamic_body> response;
    response.result(http::status::internal_server_error);
    response.set(http::field::content_type, "text/plain");
    beast::ostream(response.body()) << reason;
    writeResponse(response);
}

// gather information for response body
void HttpConnection::handleGET() {

        std::cout << "handleGET start" << std::endl; std::cout.flush();
    // todo auth

    // prevent directory traversal
    if (request.target().empty() || request.target()[0] != '/' || 
        request.target().find("..") != beast::string_view::npos) {
        
        sendBadRequest("invalid user");
        return;
    }

    //std::cout << logdir << std::endl; std::cout.flush();
    //std::cout << request.target().to_string() << std::endl; std::cout.flush();
    // /user1 -> logdir/logfile_user1
    
    auto const log_path = std::string("./logfiles/logfile_user1");
    
    // async read into response.body
    /* would need to implement boost::asio::AsyncReadStream on std::ifstream -> does not compile like this
    std::ifstream log_file (log_path);
    if (!log_file) {
        sendServerError("could not open logfile");
        return;
    }

    auto self = shared_from_this();
    net::async_read(
        log_file,
        request.body(),
        net::transfer_all(),
        [self] (beast::error_code ec, size_t bytes_read) {
            if (!ec)
                self->writeResponse();
        });
    */
    
    // read file into new http body (synchronously)
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(log_path.c_str(), beast::file_mode::scan, ec);

    // handle read errors
    if (ec==beast::errc::no_such_file_or_directory) {
        sendNotFound();
        return;
    } else if (ec) {
        sendServerError(ec.message());
        return;
    }
    
    // construct response with logfile as body
    http::response<http::file_body> res {
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, request.version()
    )}; 

        std::cout << "handleGET fin" << std::endl; std::cout.flush();

    res.result(http::status::ok);
    res.keep_alive(request.keep_alive());
    writeResponse(std::move(res));
}

void HttpConnection::handlePOST() {
    // todo
    //writeResponse();
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