#include <sap_cloud/server.h>
#include <sap_core/log.h>

namespace sap::cloud {

    Server::Server(const Config& config) : m_Config(config), m_HttpServer({-1, config.server.host, config.server.port, config.server.multithreaded}) {}

    stl::result<std::unique_ptr<Server>> Server::create(const Config& config) {
        auto server = std::unique_ptr<Server>(new Server(config));
        auto init_result = server->initialize();
        if (!init_result) {
            return stl::make_error<std::unique_ptr<Server>>("{}", init_result.error());
        }
        return server;
    }

    stl::result<> Server::initialize() {
        auto dirs_result = init_data_dirs(m_Config);
        if (!dirs_result) {
            return dirs_result;
        }
        auto meta_result = storage::MetadataStore::open(m_Config.storage.database);
        if (!meta_result) {
            return stl::make_error("Failed to open database: {}", meta_result.error());
        }
        m_Meta = std::make_unique<storage::MetadataStore>(std::move(meta_result.value()));
        m_FilesFs = std::make_unique<fs::Filesystem>(m_Config.storage.files_root);
        m_NotesFs = std::make_unique<fs::Filesystem>(m_Config.storage.notes_root);
        m_FileSvc = std::make_unique<services::FileService>(*m_FilesFs, *m_Meta);
        m_NoteSvc = std::make_unique<services::NoteService>(*m_NotesFs, *m_Meta);
        m_SyncSvc = std::make_unique<services::SyncService>(*m_FileSvc, *m_NoteSvc);
        m_Auth = std::make_unique<auth::AuthManager>(*m_Meta, m_Config.auth);
        auto auth_result = m_Auth->load_authorized_keys();
        if (!auth_result) {
            log::warn("Failed to load authorized keys: {}", auth_result.error());
        }
        setup_routes();
        auto file_scan_res = m_FileSvc->scan_and_index();
        if (!file_scan_res) {
            return stl::make_error("{}", file_scan_res.error());
        }
        auto note_scan_res = m_NoteSvc->scan_and_index();
        if (!note_scan_res) {
            return stl::make_error("{}", note_scan_res.error());
        }
        log::info("Server initialized");
        return stl::success;
    }

    void Server::run() {
        log::info("Starting server on {}:{}", m_Config.server.host, m_Config.server.port);
        auto start_result = m_HttpServer.start();
        if (!start_result) {
            log::error("Failed to start server: {}", start_result.error());
            return;
        }
        m_HttpServer.run();
    }

    void Server::stop() {
        log::info("Stopping server");
        m_HttpServer.stop();
    }

    void Server::setup_routes() {
        // Auth Routes
        m_HttpServer.route("/api/v1/auth/challenge", http::EMethod::POST,
                           [this](const http::Request& req) { return handle_auth_challenge(req); });
        m_HttpServer.route("/api/v1/auth/verify", http::EMethod::POST,
                           [this](const http::Request& req) { return handle_auth_verify(req); });
        // Sync Routes
        m_HttpServer.route("/api/v1/sync/state", http::EMethod::GET, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_sync_state(req);
        });
        // File Routes
        // Note: sap_http doesn't have path params yet, so we use prefix matching
        m_HttpServer.route("/api/v1/files", http::EMethod::GET, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_get_file(req);
        });
        m_HttpServer.route("/api/v1/files", http::EMethod::PUT, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_put_file(req);
        });
        m_HttpServer.route("/api/v1/files", http::EMethod::DELETE, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_delete_file(req);
        });
        // Note Routes
        m_HttpServer.route("/api/v1/notes", http::EMethod::GET, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            // Check if this is a list or single note request
            std::string path = req.url.path;
            if (path == "/api/v1/notes" || path == "/api/v1/notes/") {
                return handle_list_notes(req);
            }
            return handle_get_note(req);
        });
        m_HttpServer.route("/api/v1/notes", http::EMethod::POST, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_create_note(req);
        });
        m_HttpServer.route("/api/v1/notes", http::EMethod::PUT, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_update_note(req);
        });
        m_HttpServer.route("/api/v1/notes", http::EMethod::DELETE, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_delete_note(req);
        });
        m_HttpServer.route("/api/v1/notes/tags", http::EMethod::GET, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_get_tags(req);
        });
        m_HttpServer.route("/api/v1/notes/search", http::EMethod::GET, [this](const http::Request& req) {
            auto auth_result = authenticate(req);
            if (!auth_result) {
                return error_response(401, "unauthorized", auth_result.error());
            }
            return handle_search_notes(req);
        });
        log::debug("Routes configured");
    }

    stl::result<> Server::authenticate(const http::Request& req) {
        std::string auth_header = req.headers.get("Authorization");
        if (auth_header.empty()) {
            return stl::make_error("Missing Authorization header");
        }
        // Extract Bearer token
        const std::string prefix = "Bearer ";
        if (auth_header.size() <= prefix.size() || auth_header.substr(0, prefix.size()) != prefix) {
            return stl::make_error("Invalid Authorization header format");
        }
        std::string token = auth_header.substr(prefix.size());
        auto valid_header = m_Auth->validate_token(token);
        if (!valid_header) {
            return stl::make_error("{}", valid_header.error());
        }
        if (!valid_header.value()) {
            return stl::make_error("Invalid or expired token");
        }
        return stl::success;
    }

    http::Response Server::json_response(i32 status, const nlohmann::json& body) {
        http::Response resp(status, body.dump());
        resp.headers.set("Content-Type", "application/json");
        return resp;
    }

    http::Response Server::error_response(i32 status, std::string_view err, std::string_view message) {
        sync::ErrorResponse err_resp;
        err_resp.error = std::string(err);
        err_resp.message = std::string(message);
        return json_response(status, err_resp);
    }

    std::string Server::extract_path_param(const http::Request& req, std::string_view prefix) {
        std::string path = req.url.path;
        if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix) {
            return path.substr(prefix.size());
        }
        return "";
    }

    http::Response Server::handle_auth_challenge(const http::Request& req) {
        try {
            auto json = nlohmann::json::parse(req.body);
            sync::ChallengeRequest chall_req = json.get<sync::ChallengeRequest>();
            auto result = m_Auth->create_challenge(chall_req.public_key);
            if (!result) {
                return error_response(401, "auth_failed", result.error());
            }
            return json_response(200, result.value());
        } catch (const nlohmann::json::exception& e) {
            return error_response(400, "bad_request", "Invalid JSON: " + std::string(e.what()));
        }
    }

    http::Response Server::handle_auth_verify(const http::Request& req) {
        try {
            auto json = nlohmann::json::parse(req.body);
            sync::VerifyRequest verify_req = json.get<sync::VerifyRequest>();
            auto result = m_Auth->verify_challenge(verify_req);
            if (!result) {
                return error_response(401, "auth_failed", result.error());
            }
            return json_response(200, result.value());
        } catch (const nlohmann::json::exception& e) {
            return error_response(400, "bad_request", "Invalid JSON");
        }
    }

    http::Response Server::handle_sync_state(const http::Request& req) {
        std::optional<sync::Timestamp> since;
        // Parse query param: ?since=<timestamp>
        std::string query = req.url.query;
        if (!query.empty() && query[0] == '?') {
            query = query.substr(1);
        }
        auto since_pos = query.find("since=");
        if (since_pos != std::string::npos) {
            try {
                since = std::stoll(query.substr(since_pos + 6));
            } catch (...) {
                // Ignore parse errors
            }
        }
        auto result = m_SyncSvc->get_sync_state(since);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return json_response(200, result.value());
    }

    http::Response Server::handle_get_file(const http::Request& req) {
        std::string file_path = extract_path_param(req, "/api/v1/files/");
        if (file_path.empty()) {
            // List all files
            auto result = m_FileSvc->list_files();
            if (!result) {
                return error_response(500, "internal_error", result.error());
            }
            return json_response(200, result.value());
        }
        // Get specific file
        auto result = m_FileSvc->get_file(file_path);
        if (!result) {
            return error_response(404, "not_found", result.error());
        }
        http::Response resp(200, std::string(result.value().begin(), result.value().end()));
        resp.headers.set("Content-Type", "application/octet-stream");
        return resp;
    }

    http::Response Server::handle_put_file(const http::Request& req) {
        std::string file_path = extract_path_param(req, "/api/v1/files/");
        if (file_path.empty()) {
            return error_response(400, "bad_request", "File path required");
        }
        std::vector<u8> content(req.body.begin(), req.body.end());
        auto result = m_FileSvc->put_file(file_path, content);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return json_response(200, result.value());
    }

    http::Response Server::handle_delete_file(const http::Request& req) {
        std::string file_path = extract_path_param(req, "/api/v1/files/");
        if (file_path.empty()) {
            return error_response(400, "bad_request", "File path required");
        }
        auto result = m_FileSvc->delete_file(file_path);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return http::Response(204);
    }

    http::Response Server::handle_list_notes(const http::Request& req) {
        services::NoteService::ListOptions options;
        // Parse query params
        std::string query = req.url.query;
        if (!query.empty() && query[0] == '?') {
            query = query.substr(1);
        }
        // Simple query parsing
        auto tag_pos = query.find("tag=");
        if (tag_pos != std::string::npos) {
            auto end_pos = query.find('&', tag_pos);
            options.tag = query.substr(tag_pos + 4, end_pos == std::string::npos ? std::string::npos : end_pos - tag_pos - 4);
        }
        auto result = m_NoteSvc->list_notes(options);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return json_response(200, result.value());
    }

    http::Response Server::handle_get_note(const http::Request& req) {
        std::string note_id = extract_path_param(req, "/api/v1/notes/");
        if (note_id.empty()) {
            return error_response(400, "bad_request", "Note ID required");
        }
        auto result = m_NoteSvc->get_note(note_id);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        if (!result.value()) {
            return error_response(404, "not_found", "Note not found");
        }
        return json_response(200, result.value().value());
    }

    http::Response Server::handle_create_note(const http::Request& req) {
        try {
            auto json = nlohmann::json::parse(req.body);
            sync::NoteCreateRequest create_req = json.get<sync::NoteCreateRequest>();
            auto result = m_NoteSvc->create_note(create_req);
            if (!result) {
                return error_response(500, "internal_error", result.error());
            }
            return json_response(201, result.value());
        } catch (const nlohmann::json::exception& e) {
            return error_response(400, "bad_request", "Invalid JSON");
        }
    }

    http::Response Server::handle_update_note(const http::Request& req) {
        std::string note_id = extract_path_param(req, "/api/v1/notes/");
        if (note_id.empty()) {
            return error_response(400, "bad_request", "Note ID required");
        }
        try {
            auto json = nlohmann::json::parse(req.body);
            sync::NoteUpdateRequest update_req = json.get<sync::NoteUpdateRequest>();
            auto result = m_NoteSvc->update_note(note_id, update_req);
            if (!result) {
                return error_response(500, "internal_error", result.error());
            }
            return json_response(200, result.value());
        } catch (const nlohmann::json::exception& e) {
            return error_response(400, "bad_request", "Invalid JSON");
        }
    }

    http::Response Server::handle_delete_note(const http::Request& req) {
        std::string note_id = extract_path_param(req, "/api/v1/notes/");
        if (note_id.empty()) {
            return error_response(400, "bad_request", "Note ID required");
        }
        auto result = m_NoteSvc->delete_note(note_id);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return http::Response(204);
    }

    http::Response Server::handle_get_tags(const http::Request& req) {
        (void)req;
        auto result = m_NoteSvc->get_tags();
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return json_response(200, result.value());
    }

    http::Response Server::handle_search_notes(const http::Request& req) {
        std::string query = req.url.query;
        if (!query.empty() && query[0] == '?') {
            query = query.substr(1);
        }
        auto q_pos = query.find("q=");
        if (q_pos == std::string::npos) {
            return error_response(400, "bad_request", "Query parameter 'q' required");
        }
        auto end_pos = query.find('&', q_pos);
        std::string search_query = query.substr(q_pos + 2, end_pos == std::string::npos ? std::string::npos : end_pos - q_pos - 2);
        auto result = m_NoteSvc->search_notes(search_query);
        if (!result) {
            return error_response(500, "internal_error", result.error());
        }
        return json_response(200, result.value());
    }

} // namespace sap::cloud
