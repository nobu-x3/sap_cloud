#pragma once

#include <mutex>
#include <sap_cloud/config.h>
#include <sap_cloud/metadata.h>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_sync/auth.h>
#include <string>
#include <vector>

namespace sap::cloud::auth {

    // Manages SSH key-based authentication.
    // Authentication flow:
    // 1. Client sends public key to /auth/challenge
    // 2. Server generates random challenge, stores it, returns to client
    // 3. Client signs challenge with private key, sends to /auth/verify
    // 4. Server verifies signature, issues token
    // 5. Client includes token in Authorization header for all requests
    class AuthManager {
    public:
        AuthManager(storage::MetadataStore& meta, const AuthConfig& config);

        // Load authorized keys from file
        [[nodiscard]] stl::result<> load_authorized_keys();

        // Reload authorized keys (e.g., on SIGHUP)
        [[nodiscard]] stl::result<> reload_authorized_keys();

        // Generate challenge for a public key
        [[nodiscard]] stl::result<sync::AuthChallenge> create_challenge(std::string_view public_key);

        // Verify signed challenge and issue token
        [[nodiscard]] stl::result<sync::AuthToken> verify_challenge(const sync::VerifyRequest& req);

        // Validate a bearer token
        [[nodiscard]] stl::result<bool> validate_token(std::string_view token);

        // Cleanup expired tokens and challenges
        [[nodiscard]] stl::result<> cleanup_expired();

        // Check if a public key is authorized
        [[nodiscard]] bool is_authorized(std::string_view public_key);

    private:
        storage::MetadataStore& m_Meta;
        AuthConfig m_Config;
        std::vector<std::string> m_AuthorizedKeys;
        mutable std::mutex m_KeysMutex;
    };

} // namespace sap::cloud::auth
