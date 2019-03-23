#include <boost/beast/core/detail/base64.hpp> // caution: could move somwhere else
#include <boost/optional.hpp>
#include <exception> // std::out_of_range for http::basic_fields::at
#include <fstream>
#include <set> // verifyJWT()

#include "http_server.hpp"
#include "../include/jwt-cpp/jwt.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace fs = std::filesystem;

void fail(std::error_code const& ec, std::string const& msg) {
    std::cerr << msg << ": " << ec.message() << std:: endl;
}

void start_http_server(tcp::acceptor& acceptor, 
                        tcp::socket& socket, 
                        fs::path const& logdir,
                        std::pair<std::string,std::string> const& keypair,                        
                        std::string const& server_name) {
    acceptor.async_accept(socket, 
        [&](beast::error_code ec) {
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
            return handlePOST();
        case http::verb::head: // (maybe last logfile update timepoint for  req. user)
            // falltrhugh until implemented
        default:
            return writeResponse(BadRequest("invalid request method"));
    }
}

// gather information for response body
// GET /user1 HTTP/1.1\r\n\r\n -> send /logdir/user1.log
void HttpConnection::handleGET() {

    // check if target of type /user
    if (request_.target().empty() || request_.target()[0] != '/' || 
        request_.target().find("..") != beast::string_view::npos || // prevent directory traversal
        request_.target().find('/') != request_.target().rfind('/')) // only 1 '/' allowed
    {
        return writeResponse(BadRequest("invalid request target"));
    }

    if (request_.target() == "/pubkey")
        return writeResponse(PubKeyResponse());

    // /user1 -> logdir/user1.log
    auto const target_user = request_.target().substr(1).to_string(); // string_view::substr doesnt return std::string
    auto const logfile_path = (logdir_ / fs::path(target_user)).replace_extension(".log");

    // LogfileResponse() requires existing path
    if (!std::filesystem::exists(logfile_path))
        return writeResponse(NotFound(request_.target()));

    // verify JWT, write response with logfile if ok
    boost::optional<boost::string_view> token = extractJWT();
    if (!token)
        return writeResponse(Unauthorized("missing or malformed token"));
    if (!verifyJWT(token.get().to_string(), target_user)) {
        return writeResponse(Unauthorized("invalid token provided"));
    }

    // construct and send response containing requested logfile
    return writeResponse(LogfileResponse(logfile_path));
}

/*  http basic auth (field authorization with value "Basic "+base64(user:pw))
    returns 0 and fills auth_user and auth_pass if successful
    -1 Header field Authentification not present
    -2 auth method != Basic
    -3 auth credentials malformed    */
int HttpConnection::getBasicAuthCredentials(std::string& auth_user, std::string& auth_pass) const {

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
    // todo: write encrypted
    
    if (request_.body().size() < 1)
        return writeResponse(BadRequest("empty message"));

    // /user1 -> logdir/user1.log
    auto const target_user = request_.target().substr(1).to_string(); // string_view::substr doesnt return std::string
    auto const logfile_path = (logdir_ / fs::path(target_user)).replace_extension(".log");

    // if there already is an entry for requested user, valid token needs to be provided
    if (fs::exists(logfile_path)) {

        // verify token, write file if ok, write response
        boost::optional<boost::string_view> token = extractJWT();
        if (!token)
            return writeResponse(Unauthorized("trying to append existing log but no or malformed token provided"));

        if (verifyJWT(token.get().to_string(), target_user)) {

            if (writeLogfile(logfile_path) < 0)
                return writeResponse(ServerError("could not open logfile")); 
                   
            return writeResponse(PostOkResponse(token.get().to_string()));  
        } 
        else
            return writeResponse(Unauthorized("invalid token provided"));
    } 
    else {
        // write file, generate new token, write response

        if (writeLogfile(logfile_path) < 0)
            return writeResponse(ServerError("could not open logfile"));            

        auto alg = jwt::algorithm::rs256 {pub_key_, priv_key_, "", ""};
        auto new_token = jwt::create()
            .set_type("JWT")
            .set_issuer(server_name_)
            .set_audience(target_user)
            .sign(alg);

        return writeResponse(PostOkResponse(new_token));
    }
    assert(false); // should already have returned in if/else
}

// append request body to logfile
// returns 0 on success or -1 if file couldnot be opened
int HttpConnection::writeLogfile(fs::path const& full_path) const {
    
    std::ofstream logfile (full_path, std::ios_base::app);
    if (!logfile)
        return -1;
    
    // TODO: find more elegant way
    for (auto part : request_.body().data()) {
        auto buf_start = net::buffer_cast<const char*>(part);
        auto buf_last = buf_start + net::buffer_size(part);
        std::copy(buf_start, buf_last, std::ostream_iterator<char>(logfile));
    }
    
    return 0;
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

// get JWT string out of request header
boost::optional<boost::string_view> HttpConnection::extractJWT() const {

    try {
        boost::string_view token_encoded = request_.at(http::field::authorization);

        if (beast::iequals(token_encoded.substr(0,6), "Bearer")) {
            token_encoded.remove_prefix(7);
            return boost::optional<boost::string_view>(token_encoded);
        } else return boost::none; // wrong authentification method

    } catch (std::out_of_range&) { // no Authorization header field
        return boost::none;
    }
}

// take token string (encoded) and verify signature
// return true on success, false otherwise
bool HttpConnection::verifyJWT(std::string const& token, std::string const& requested_user) const {
    
    std::unique_ptr<jwt::decoded_jwt> token_decoded;
    try {
        token_decoded = std::make_unique<jwt::decoded_jwt>(jwt::decode(token));
    } catch (std::runtime_error& e) {
        std::cerr << "jwt::decode(): " << e.what() << std::endl;
        return false;
    }
    auto alg_used = token_decoded->get_algorithm();

    std::set<std::string> audience;
    audience.insert(requested_user);
    auto verifier = jwt::verify()
        .allow_algorithm(jwt::algorithm::rs256{pub_key_, priv_key_})
        .with_issuer(server_name_)
        .with_audience(audience);

    try {
        verifier.verify(*token_decoded);
    } catch (jwt::token_verification_exception& e) {
        std::cerr << "verifyJWT(): " << e.what() << std::endl;
        return false;
    }
    
    return true;
}