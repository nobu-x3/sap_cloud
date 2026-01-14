#include <cstdlib>
#include <fstream>
#include <sap_cloud/config.h>
#include <sap_core/log.h>
#include <toml++/toml.hpp>

namespace sap::cloud {

    namespace fs = std::filesystem;

    std::filesystem::path get_data_dir() {
        const char* home = std::getenv("HOME");
        if (!home) {
            home = std::getenv("USERPROFILE"); // Windows
        }
        if (!home) {
            return {};
        }
        return fs::path(home) / ".sapcloud";
    }

    stl::result<Config> load_config(const fs::path& path) {
        Config config;
        try {
            auto tbl = toml::parse_file(path.string());
            // Server section
            if (auto server = tbl["server"].as_table()) {
                if (auto host = (*server)["host"].value<std::string>()) {
                    config.server.host = *host;
                }
                if (auto port = (*server)["port"].value<i64>()) {
                    config.server.port = static_cast<u16>(*port);
                }
                if (auto mt = (*server)["multithreaded"].value<bool>()) {
                    config.server.multithreaded = *mt;
                }
            }
            // Storage section
            auto data_dir = get_data_dir();
            config.storage.files_root = data_dir / "files";
            config.storage.notes_root = data_dir / "notes";
            config.storage.database = data_dir / "sap_drive.db";
            if (auto storage = tbl["storage"].as_table()) {
                if (auto fr = (*storage)["files_root"].value<std::string>()) {
                    config.storage.files_root = *fr;
                }
                if (auto nr = (*storage)["notes_root"].value<std::string>()) {
                    config.storage.notes_root = *nr;
                }
                if (auto db = (*storage)["database"].value<std::string>()) {
                    config.storage.database = *db;
                }
            }
            // Auth section
            config.auth.authorized_keys = data_dir / "authorized_keys";
            if (auto auth = tbl["auth"].as_table()) {
                if (auto ak = (*auth)["authorized_keys"].value<std::string>()) {
                    config.auth.authorized_keys = *ak;
                }
                if (auto te = (*auth)["token_expiry"].value<i64>()) {
                    config.auth.token_expiry = *te;
                }
                if (auto ce = (*auth)["challenge_expiry"].value<i64>()) {
                    config.auth.challenge_expiry = *ce;
                }
            }
            // Logging section
            if (auto logging = tbl["logging"].as_table()) {
                if (auto level = (*logging)["level"].value<std::string>()) {
                    config.logging.level = *level;
                }
            }
            return config;
        } catch (const toml::parse_error& err) {
            return stl::make_error<Config>("Failed to parse config: {}", std::string(err.description()));
        }
    }

    stl::result<Config> load_config_default() {
        // Try locations in order
        std::vector<fs::path> locations = {get_data_dir() / "sap_drive.toml", "/etc/sap_drive/sap_drive.toml"};
        for (const auto& path : locations) {
            if (fs::exists(path)) {
                log::info("Loading config from: {}", path.string());
                return load_config(path);
            }
        }
        // No config file found, use defaults
        log::info("No config file found, using defaults");
        Config config;
        auto data_dir = get_data_dir();
        config.storage.files_root = data_dir / "files";
        config.storage.notes_root = data_dir / "notes";
        config.storage.database = data_dir / "sap_drive.db";
        config.auth.authorized_keys = data_dir / "authorized_keys";
        return config;
    }

    stl::result<> init_data_dirs(const Config& config) {
        std::error_code ec;
        // Create data directories
        fs::create_directories(config.storage.files_root, ec);
        if (ec) {
            return stl::make_error("Failed to create files directory: {}", ec.message());
        }
        fs::create_directories(config.storage.notes_root, ec);
        if (ec) {
            return stl::make_error("Failed to create notes directory: {}", ec.message());
        }
        // Ensure database parent directory exists
        if (config.storage.database.has_parent_path()) {
            fs::create_directories(config.storage.database.parent_path(), ec);
            if (ec) {
                return stl::make_error("Failed to create database directory: {}", ec.message());
            }
        }
        // Create authorized_keys file if it doesn't exist
        if (!fs::exists(config.auth.authorized_keys)) {
            if (config.auth.authorized_keys.has_parent_path()) {
                fs::create_directories(config.auth.authorized_keys.parent_path(), ec);
            }
            std::ofstream f(config.auth.authorized_keys);
            if (!f) {
                log::warn("Could not create authorized_keys file");
            }
        }
        return stl::success;
    }

} // namespace sap::cloud
