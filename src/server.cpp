#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>

#include "http_server.hpp"
#include "../include/cpp-jwt/jwt/jwt.hpp"

using namespace std;

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// read key pair from provided paths into strings (required by jwt implementation)
// exits on error
// pair.first -> public key
// pair.second -> private_key
pair<string, string> read_rsa_keys(filesystem::path const& pub_key, filesystem::path const& priv_key) {
    
    if (!filesystem::exists(priv_key) || !filesystem::exists(pub_key)) {
        cerr << "could not find provided key files" << endl;
        exit(EXIT_FAILURE);
    }

    pair<string, string> keypair;
    array<filesystem::path, 2> keyfile_paths {pub_key, priv_key};
    for (int i=0; i<2; i++) {

        auto path = keyfile_paths[i];
        std::ifstream keyfile (path);

        if (!keyfile) {
            cerr << "[ERROR] could not open \"" << path << "\"" << endl;
            exit(EXIT_FAILURE); 
        }

        const string pattern_private_key ("-----BEGIN PRIVATE KEY-----");
        const string pattern_public_key ("-----BEGIN PUBLIC KEY-----");

        keyfile.seekg(0, std::ios::end);
        size_t fsize = keyfile.tellg();
        keyfile.seekg(0);

        string first_line;
        first_line.resize(pattern_private_key.size()+1);
        keyfile.getline(first_line.data(), first_line.size());

        if (first_line.find(pattern_public_key) == 0) {
            keyfile.seekg(0); // read from start again to get full file
            keypair.first.resize(fsize);
            keyfile.read(keypair.first.data(), fsize);
        }
        else if (first_line.find(pattern_private_key) == 0) {
            keyfile.seekg(0);
            keypair.second.resize(fsize);
            keyfile.read(keypair.second.data(), fsize);
        }
        else {
            cerr << "[ERROR] rsa key malformed: \"" << path << "\"" << endl;
            keyfile.close();
            exit(EXIT_FAILURE);
        }
        keyfile.close();

    }

    // test if jwt implementation actually takes the provided keys
    try {
        namespace jwtp = jwt::params;

        auto token = jwt::jwt_object {jwtp::algorithm("RS256"), jwtp::secret(keypair.second)};
        auto token_str = token.signature();

        auto decoded = jwt::decode(token_str, jwtp::algorithms({"RS256"}), jwtp::secret(keypair.first));

    } catch (jwt::SigningError const& e){
        cerr << "[ERROR] rsa priv key malformed: " << e.what() << endl;
        exit(EXIT_FAILURE);
    } catch (jwt::DecodeError const& e) {
        cerr << "[ERROR] rsa pub key malformed: " << e.what() << endl;
        exit(EXIT_FAILURE);
    }

    return keypair;
}

int main(int argc, char** argv) {

    if (argc != 2) return EXIT_FAILURE;

    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));

    net::io_context ioc {1};

    tcp::acceptor acc {ioc, {net::ip::make_address("0.0.0.0"), port}};
    tcp::socket sock {ioc};

    auto const logdir = filesystem::path("./logfiles/");
    auto const pub_key = filesystem::path("./rsa_keys/public_key.pem");
    auto const priv_key = filesystem::path("./rsa_keys/private_key.pem");

    pair<string,string> keypair = read_rsa_keys(pub_key, priv_key);

    start_http_server(acc, sock, logdir, keypair, "logserver v0.1");

    // register SIGINT and SIGTERM handler
    net::signal_set signals {ioc, SIGINT, SIGTERM};
    signals.async_wait(
        [&] (beast::error_code const&, int) {
            cout << "\nsignal recieved. stopping server" << endl;
            ioc.stop();
        });

    cout << "started server on port " << port << endl;
    ioc.run();

    return EXIT_SUCCESS;
}