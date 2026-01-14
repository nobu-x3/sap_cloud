#include <csignal>
#include <filesystem>
#include <iostream>
#include <sap_cloud/config.h>
#include <sap_cloud/server.h>
#include <sap_core/log.h>

namespace {
    sap::cloud::Server* g_Server = nullptr;
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        sap::log::info("Received signal {}, shutting down...", sig);
        if (g_Server) {
            g_Server->stop();
        }
    }
}

void print_usage(const char* progname) {
    std::cout << "Usage: " << progname << " [options]\n"
              << "\nOptions:\n"
              << "  -c, --config <path>   Path to config file\n"
              << "  -h, --help            Show this help message\n"
              << "  -v, --version         Show version\n"
              << "\nDefault config locations:\n"
              << "  ~/.sapcloud/sap_drive.toml\n"
              << "  /etc/sap_drive/sap_drive.toml\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::filesystem::path config_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "sap_drive v0.1.0\n";
            return 0;
        }
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }
    // Load configuration
    sap::stl::result<sap::cloud::Config> config_result;
    if (!config_path.empty()) {
        sap::log::info("Loading config from: {}", config_path.string());
        config_result = sap::cloud::load_config(config_path);
    } else {
        config_result = sap::cloud::load_config_default();
    }
    if (!config_result) {
        sap::log::error("Failed to load config: {}", config_result.error());
        return 1;
    }
    auto& config = config_result.value();
    // Create server
    auto server_result = sap::cloud::Server::create(config);
    if (!server_result) {
        sap::log::error("Failed to create server: {}", server_result.error());
        return 1;
    }
    auto& server = server_result.value();
    g_Server = server.get();
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    // Run server
    sap::log::info("sap_cloud v0.1.0 starting...");
    sap::log::info("Data directory: {}", sap::cloud::get_data_dir().string());
    sap::log::info("Files root: {}", config.storage.files_root.string());
    sap::log::info("Notes root: {}", config.storage.notes_root.string());
    server->run();
    sap::log::info("Server stopped");
    return 0;
}