#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <sap_cloud/auth_manager.h>
#include <sap_cloud/config.h>
#include <sap_cloud/metadata.h>
#include <sap_cloud/services/file_service.h>
#include <sap_cloud/services/notes_service.h>
#include <sap_cloud/services/sync_service.h>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_fs/fs.h>
#include <sap_http/net/http.h>
#include <sap_sync/sync_types.h>

namespace sap::cloud {

    // Main server class that sets up HTTP routes and coordinates services.
    class Server {
    public:
        // Initialize server with configuration
        static stl::result<std::unique_ptr<Server>> create(const Config& config);

        // Start the server (blocking)
        void run();

        // Stop the server
        void stop();

    private:
        explicit Server(const Config& config);

        stl::result<> initialize();

        // Setup HTTP routes
        void setup_routes();

        // Auth routes
        http::Response handle_auth_challenge(const http::Request& req);

        http::Response handle_auth_verify(const http::Request& req);

        // Sync routes
        http::Response handle_sync_state(const http::Request& req);

        // File routes
        http::Response handle_get_file(const http::Request& req);

        http::Response handle_put_file(const http::Request& req);

        http::Response handle_delete_file(const http::Request& req);

        // Note routes
        http::Response handle_list_notes(const http::Request& req);

        http::Response handle_get_note(const http::Request& req);

        http::Response handle_create_note(const http::Request& req);

        http::Response handle_update_note(const http::Request& req);

        http::Response handle_delete_note(const http::Request& req);

        http::Response handle_get_tags(const http::Request& req);

        http::Response handle_search_notes(const http::Request& req);

        // Extract and validate auth token
        stl::result<> authenticate(const http::Request& req);

        // JSON response helpers
        http::Response json_response(i32 status, const nlohmann::json& body);

        http::Response error_response(i32 status, std::string_view error, std::string_view message);

        // Extract path parameter (e.g., /notes/{id} -> id)
        std::string extract_path_param(const http::Request& req, std::string_view prefix);

        Config m_Config;
        http::Server m_HttpServer;

        // Storage
        std::unique_ptr<fs::Filesystem> m_FilesFs;
        std::unique_ptr<fs::Filesystem> m_NotesFs;
        std::unique_ptr<storage::MetadataStore> m_Meta;

        // Services
        std::unique_ptr<services::FileService> m_FileSvc;
        std::unique_ptr<services::NoteService> m_NoteSvc;
        std::unique_ptr<services::SyncService> m_SyncSvc;

        // Auth
        std::unique_ptr<auth::AuthManager> m_Auth;
    };

} // namespace sap::cloud
