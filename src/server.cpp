#include <iostream>
#include <memory>
#include <filesystem>

#include "logtypes.hpp"
#include "http_server.hpp"

using namespace std;

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

int main(int argc, char** argv) {

    if (argc != 2) return EXIT_FAILURE;

    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));

    net::io_context ioc {1};

    tcp::acceptor acc {ioc, {net::ip::make_address("0.0.0.0"), port}};
    tcp::socket sock {ioc};

    auto const logdir = make_shared<filesystem::path>("./logfiles/");
    start_http_server(acc, sock, logdir);

    // register SIGINT and SIGTERM handler
    net::signal_set signals {ioc, SIGINT, SIGTERM};
    signals.async_wait(
        [&] (beast::error_code const&, int) {
            ioc.stop();
        });

    cout << "started server on port " << port << endl;
    ioc.run();

    return EXIT_SUCCESS;
}