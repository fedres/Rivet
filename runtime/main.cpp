// runtime/main.cpp — Rivet runtime entry point
#include "cli/cli.hpp"
#include "../platform/interface/env.hpp"
#include "../platform/interface/net.hpp"
#include "../platform/interface/terminal.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    // Enable VT/ANSI processing on Windows 10+ consoles.
    rivet::terminal::enable_vt_processing(1);
    rivet::terminal::enable_vt_processing(2);

    // Initialize TLS trust store for all network operations.
    if (auto r = rivet::net::init_tls_trust_store(); !r) {
        std::cerr << "warning: failed to initialize TLS trust store: "
                  << r.error().message << "\n";
    }

    // Dispatch to CLI router.
    rivet::cli::Context ctx{argc, argv};
    return rivet::cli::run(ctx);
}
