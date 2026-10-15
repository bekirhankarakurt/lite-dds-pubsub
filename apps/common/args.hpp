#pragma once

// Tiny declarative command-line parser shared by the example tools.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ldds/transport.hpp"

namespace app {

enum class Kind { Flag, Text, Int, Real };

struct Option {
    std::string name;
    Kind kind;
    std::string help;
    std::string default_value;
};

class Args {
public:
    Args(std::string program, std::string summary, std::vector<Option> options)
        : program_(std::move(program)), summary_(std::move(summary)), options_(std::move(options)) {
        for (const Option& o : options_) {
            if (o.kind != Kind::Flag) values_[o.name] = o.default_value;
        }
    }

    /// Returns false when the program should exit with exit_code() (help shown or bad input).
    bool parse(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                usage(std::cout);
                exit_code_ = 0;
                return false;
            }
            if (arg.rfind("--", 0) != 0) return fail("unexpected argument '" + arg + "'");
            std::string inline_value;
            bool has_inline = false;
            const std::size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                inline_value = arg.substr(eq + 1);
                has_inline = true;
                arg.resize(eq);
            }
            const Option* opt = find(arg.substr(2));
            if (opt == nullptr) return fail("unknown option '" + arg + "'");
            if (opt->kind == Kind::Flag) {
                if (has_inline) return fail("option '" + arg + "' takes no value");
                flags_[opt->name] = true;
                continue;
            }
            if (!has_inline) {
                if (i + 1 >= argc) return fail("option '" + arg + "' needs a value");
                inline_value = argv[++i];
            }
            if (!valid(*opt, inline_value)) return fail("invalid value '" + inline_value + "' for '" + arg + "'");
            values_[opt->name] = inline_value;
        }
        return true;
    }

    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] bool flag(const std::string& name) const { return flags_.count(name) != 0; }
    [[nodiscard]] const std::string& text(const std::string& name) const { return values_.at(name); }
    [[nodiscard]] std::int64_t integer(const std::string& name) const {
        return std::strtoll(values_.at(name).c_str(), nullptr, 10);
    }
    [[nodiscard]] double real(const std::string& name) const {
        return std::strtod(values_.at(name).c_str(), nullptr);
    }

private:
    [[nodiscard]] const Option* find(const std::string& name) const {
        for (const Option& o : options_) {
            if (o.name == name) return &o;
        }
        return nullptr;
    }

    static bool valid(const Option& o, const std::string& v) {
        if (o.kind == Kind::Text) return true;
        if (v.empty()) return false;
        char* end = nullptr;
        if (o.kind == Kind::Int) {
            (void)std::strtoll(v.c_str(), &end, 10);
        } else {
            (void)std::strtod(v.c_str(), &end);
        }
        return *end == '\0';
    }

    bool fail(const std::string& message) {
        std::cerr << program_ << ": " << message << "\n\n";
        usage(std::cerr);
        exit_code_ = 2;
        return false;
    }

    void usage(std::ostream& os) const {
        os << summary_ << "\n\nUsage: " << program_ << " [options]\n\nOptions:\n";
        for (const Option& o : options_) {
            std::string left = "  --" + o.name;
            if (o.kind != Kind::Flag) left += " <" + std::string(o.kind == Kind::Text ? "text" : o.kind == Kind::Int ? "int" : "num") + ">";
            os << left << std::string(left.size() < 28 ? 28 - left.size() : 1, ' ') << o.help;
            if (!o.default_value.empty()) os << " (default: " << o.default_value << ")";
            os << '\n';
        }
        os << "  -h, --help" << std::string(18, ' ') << "show this help\n";
    }

    std::string program_;
    std::string summary_;
    std::vector<Option> options_;
    std::map<std::string, std::string> values_;
    std::map<std::string, bool> flags_;
    int exit_code_{2};
};

/// Options shared by every tool that talks to the network.
inline std::vector<Option> network_options() {
    return {
        {"group", Kind::Text, "multicast group", "239.255.42.99"},
        {"port", Kind::Int, "UDP port", "7447"},
        {"iface", Kind::Text, "local interface address for multicast", "127.0.0.1"},
    };
}

inline ldds::UdpConfig udp_config(const Args& args) {
    ldds::UdpConfig cfg;
    cfg.group = args.text("group");
    cfg.port = static_cast<std::uint16_t>(args.integer("port"));
    cfg.interface_addr = args.text("iface");
    return cfg;
}

}  // namespace app
