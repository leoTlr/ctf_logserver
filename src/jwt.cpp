#ifndef LOGSERVER_JWT_CPP
#define LOGSERVER_JWT_CPP

#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/detail/base64.hpp> // caution private namespace (could change anytime)
#include <algorithm>
#include <string>

namespace pt = boost::property_tree;
using boost::beast::detail::base64_decode;

class jwt {

    pt::ptree header_, payload_;
    std::vector<uint8_t> sig_;

public:
    // construct empty jwt
    jwt() = default;

    // TODO: waaay to hacky
    // takes jwt string to decode and fill internal ptrees
    jwt(std::string_view token) {
        // header.payload.sig
        assert(std::count(token.begin(), token.end(), '.') == 2);

        // base64url -> base64
        std::string token_b64;
        token_b64.resize(token.size());
        std::transform(
            std::begin(token),
            std::end(token),
            std::begin(token_b64),
            [] (auto ch) {
                if (ch == '-') return '+';
                else if (ch == '_') return '/';
                else return ch;
            });

        const std::size_t payload_start = token_b64.find('.') + 1;
        const std::size_t sig_start = token_b64.rfind('.') + 1;
    
        const std::string header_encoded = token_b64.substr(0, payload_start-1);
        const std::string payload_encoded = token_b64.substr(payload_start, (sig_start-1-payload_start));
        const std::string sig_encoded = token_b64.substr(sig_start);

        // ensure substrings only contain the b64-part
        assert(header_encoded.find('.') == std::string::npos);
        assert(payload_encoded.find('.') == std::string::npos);
        assert(sig_encoded.find('.') == std::string::npos);

        // pt::read_json needs stringstream, not string
        std::stringstream header_decoded (base64_decode(header_encoded));
        std::stringstream payload_decoded (base64_decode(payload_encoded));
        sig_.resize(sig_encoded.size());
        boost::beast::detail::base64::decode( &(sig_[0]), sig_encoded.data(), sig_encoded.size());

        // parse plain json strings into ptrees
        pt::read_json(header_decoded, header_);
        pt::read_json(payload_decoded, payload_);
    }

    // TODO
    bool verify(std::string_view token, std::string_view signature) const;
    void set_claim(std::string const& name, std::string const& value);
    std::string get_claim(std::string const& name) const;
};

#endif