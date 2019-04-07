#include <boost/beast/core/detail/base64.hpp> // caution: could move somwhere else
#include <boost/optional.hpp>
#include <exception> // std::out_of_range for http::basic_fields::at
#include <fstream>

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

// struct query_params.nr_entries:
//  X in /request_target?entires=X&foo=bar
//  0 if no entries query or entries=0
//  -1 if no query
//  -2 if bad value
// struct query_params.new_user: name of new user (only used if target==/adduser?name=some_user)
struct query_params HttpConnection::parseTargetQuery() const {
    size_t query_pos = 0, separator_pos = 0;
    struct query_params params {0, false, ""};

    if ((query_pos = request_.target().find('?')) == boost::string_view::npos) {
        params.nr_entries = -1;
        return params; // no '?' in target -> no queries to parse
    }

    // loop through all queries splitting keys and values and adding values to return struct
    do  {
        separator_pos = request_.target().find('=', query_pos);
        boost::string_view query = request_.target().substr(query_pos + 1, separator_pos - query_pos - 1);

        if (beast::iequals(query, "entries")) {

            size_t len_str_val = request_.target().find('&', separator_pos) - separator_pos;
            const auto val = request_.target().substr(separator_pos + 1, len_str_val);

            int nr_entries = 0;
            try {
                nr_entries = std::stoi(val.to_string());
            } catch (...) {
                params.nr_entries = -2;
                continue; // str->int conversion failed
            }
            // val < 0 -> invalid val
            params.nr_entries = nr_entries >= 0 ? nr_entries : -2;
        } 
        else if (beast::iequals(query, "debug")) {
            size_t len_str_val = request_.target().find('&', separator_pos) - separator_pos;
            const auto val = request_.target().substr(separator_pos + 1, len_str_val);

            if (beast::iequals(val, "true"))
                params.debug = true;
        }
        else if (beast::iequals(query, "name")) {
            size_t len_str_val = request_.target().find('&', separator_pos) - separator_pos;
            const auto val = request_.target().substr(separator_pos + 1, len_str_val);

            params.new_user = val;
        }
    } while ((query_pos = request_.target().find('&', query_pos + 1)) != boost::string_view::npos);    

    return params;
}

// /user?query=foo -> user
boost::string_view HttpConnection::getTarget() const {
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

    if (getTarget() == "pubkey")
        return writeResponse(PubKeyResponse());

    // /user1?query=foo -> logdir/user1.log
    auto const target_user = getTarget().to_string();
    auto const logfile_path = (logdir_ / fs::path(target_user)).replace_extension(".log");

    if (target_user == "adduser") {
        auto query = parseTargetQuery();

        if (query.new_user.empty())
            return writeResponse(BadRequest("no name provided. try /adduser?name=uname"));

        // maybe remove this check for other possible exploit
        fs::path new_user_filepath = (logdir_ / fs::path(query.new_user.to_string())).replace_extension(".log");
        if (fs::exists(new_user_filepath))
            return writeResponse(Unauthorized("user already exists"));

        std::ofstream new_logfile {new_user_filepath};
        if (!new_logfile)
            return writeResponse(ServerError("could not create empty logfile for given user"));
        
        auto new_token = newToken(query.new_user.to_string());
        return writeResponse(tokenResponse(new_token));
    }

    // LogfileResponse() requires existing path
    if (!std::filesystem::exists(logfile_path))
        return writeResponse(NotFound(target_user));

    struct query_params query = parseTargetQuery();    

    // verify JWT, write response with logfile if ok
    boost::optional<boost::string_view> token = extractJWT();
    if (!token)
        return writeResponse(Unauthorized("missing or malformed token"));
    if (!verifyJWT(token.get().to_string(), target_user) && !query.debug) {
        return writeResponse(Unauthorized("invalid token provided"));
    }

    if (query.nr_entries > 0)
        return writeResponse(LastLogsResponse(logfile_path, query.nr_entries));
    return writeResponse(LogfileResponse(logfile_path));
}

void HttpConnection::handlePOST() {
    // todo: write encrypted
    
    if (request_.body().size() < 1)
        return writeResponse(BadRequest("empty message"));

    // /user1 -> logdir/user1.log
    auto const target_user = getTarget().to_string();
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
                   
            return writeResponse(tokenResponse(token.get().to_string()));  
        } 
        else
            return writeResponse(Unauthorized("invalid token provided"));
    } 
    else {
        // write file, generate new token, write response

        if (writeLogfile(logfile_path) < 0)
            return writeResponse(ServerError("could not open logfile"));            

        return writeResponse(tokenResponse(newToken(target_user)));
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

std::string HttpConnection::newToken(std::string const& name) const {

    auto new_token = jwt::jwt_object {
        jwt::params::algorithm("RS256"),
        jwt::params::secret(priv_key_)
    };

    new_token.add_claim("iss", server_name_);
    new_token.add_claim("aud", name);

    return new_token.signature();
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
    } catch (jwt::DecodeError const& e) { // malformed token
        return false;
    } catch (jwt::VerificationError const& e) { // invalid sig
        return false;
    }
    
    return true;
}