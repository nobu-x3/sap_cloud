#pragma once

#include <filesystem>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <string>

namespace sap::cloud {

    // Server configuration loaded from TOML file.
    // Default locations:
    //   1. Path specified via command line
    //   2. ~/.sapcloud/sap_drive.toml
    //   3. /etc/sap_drive/sap_drive.toml
    struct ServerConfig {
        std::string host = "127.0.0.1";
        u16 port = 8080;
        bool multithreaded = true;
    };

    struct StorageConfig {
        std::filesystem::path files_root; // Root for generic files
        std::filesystem::path notes_root; // Root for notes
        std::filesystem::path database; // SQLite database path
    };

    struct AuthConfig {
        std::filesystem::path authorized_keys; // SSH authorized_keys file
        i64 token_expiry = 86400; // Token lifetime (seconds)
        i64 challenge_expiry = 300; // Challenge lifetime (seconds)
    };

    struct LoggingConfig {
        std::string level = "info"; // debug, info, warn, error
    };

    struct Config {
        ServerConfig server;
        StorageConfig storage;
        AuthConfig auth;
        LoggingConfig logging;
    };

    // Load configuration from file
    stl::result<Config> load_config(const std::filesystem::path& path);

    // Load configuration from default locations
    stl::result<Config> load_config_default();

    // Get the data directory (~/.sapcloud)
    std::filesystem::path get_data_dir();

    // Initialize data directories (creates if not exist)
    stl::result<> init_data_dirs(const Config& config);

} // namespace sap::cloud
