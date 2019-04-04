#include <boost/beast/core/detail/base64.hpp> // caution: could move somwhere else
#include <boost/optional.hpp>
#include <exception> // std::out_of_range for http::basic_fields::at
#include <fstream>
#include <map>
#include <set>

#include "http_server.hpp"
#include "../include/cpp-jwt/jwt/jwt.hpp"

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

// /user?query=foo -> user
boost::string_view HttpConnection::getTargetUser() const {
    size_t pos = 0;
    if ((pos = request_.target().find('?')) == boost::string_view::npos)
        return request_.target().substr(1);
    return request_.target().substr(1, pos - 1);
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

    // /user1?query=foo -> logdir/user1.log
    auto const target_user = getTargetUser().to_string();
    std::cout << target_user << std::endl;
    std::cout << target_user << "  " << request_.target().find('?') << std:: endl;
    auto const logfile_path = (logdir_ / fs::path(target_user)).replace_extension(".log");

    // LogfileResponse() requires existing path
    if (!std::filesystem::exists(logfile_path))
        return writeResponse(NotFound(target_user));

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

void HttpConnection::handlePOST() {
    // todo: write encrypted
    
    if (request_.body().size() < 1)
        return writeResponse(BadRequest("empty message"));

    // /user1 -> logdir/user1.log
    auto const target_user = getTargetUser().to_string();
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

        auto new_token = jwt::jwt_object {
            jwt::params::algorithm("RS256"),
            jwt::params::secret(priv_key_)
        };

        new_token.add_claim("iss", server_name_);
        new_token.add_claim("aud", target_user);

        return writeResponse(PostOkResponse(new_token.signature()));
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

    try {
        // get alg from token header
        auto header = jwt::jwt_header{token};
        auto alg_used = jwt::alg_to_str(header.algo());
        
        if (alg_used == jwt::string_view{"NONE"})
            return false;

        std::error_code ec;
        auto decoded_token = jwt::decode(
                jwt::string_view(token), 
                jwt::params::algorithms({alg_used}), 
                ec,
                jwt::params::secret(pub_key_),
                jwt::params::issuer(server_name_),
                jwt::params::aud(requested_user), // request target has to match audience
                jwt::params::verify(true));

        if (ec) // invalid issuer or audience
            return false;

    } catch (jwt::SignatureFormatError const& e) { // malformed sig
        return false;
    } catch (jwt::DecodeError const& e) {
        return false;
    } catch (jwt::VerificationError const& e) { // invalid sig
        return false;
    }
    
    return true;
}