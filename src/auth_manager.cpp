#include "sap_cloud/auth_manager.h"
#include <sap_core/log.h>

namespace sap::cloud::auth {

    AuthManager::AuthManager(storage::MetadataStore& meta, const AuthConfig& config) : m_Meta(meta), m_Config(config) {}

    stl::result<> AuthManager::load_authorized_keys() {
        std::lock_guard<std::mutex> lock(m_KeysMutex);
        auto keys_result = sync::load_authorized_keys(m_Config.authorized_keys);
        if (!keys_result) {
            return stl::make_error<>("{}", keys_result.error());
        }
        m_AuthorizedKeys = std::move(keys_result.value());
        log::info("Loaded {} authorized keys", m_AuthorizedKeys.size());
        return stl::success;
    }

    stl::result<> AuthManager::reload_authorized_keys() { return load_authorized_keys(); }

    stl::result<sync::AuthChallenge> AuthManager::create_challenge(std::string_view public_key) {
        // Verify key is authorized
        if (!is_authorized(public_key)) {
            return stl::make_error<sync::AuthChallenge>("Key not authorized");
        }
        // Parse key to verify format
        auto key_result = sync::parse_public_key(public_key);
        if (!key_result) {
            return stl::make_error<sync::AuthChallenge>("Invalid public key format: {}", key_result.error());
        }
        // Generate challenge
        std::string challenge = sync::generate_challenge();
        i64 expires_at = (sync::now_ms() / 1000) + m_Config.challenge_expiry;
        // Store challenge
        auto store_result = m_Meta.store_challenge(challenge, public_key, expires_at);
        if (!store_result) {
            return stl::make_error<sync::AuthChallenge>("{}", store_result.error());
        }
        sync::AuthChallenge resp;
        resp.challenge = challenge;
        resp.public_key = std::string(public_key);
        resp.expires_at = expires_at;
        log::debug("Created challenge for key: {}...", std::string(public_key).substr(0, 30));
        return resp;
    }

    stl::result<sync::AuthToken> AuthManager::verify_challenge(const sync::VerifyRequest& req) {
        // Validate challenge exists and matches public key
        auto valid_result = m_Meta.validate_challenge(req.challenge, req.public_key);
        if (!valid_result) {
            return stl::make_error<sync::AuthToken>("{}", valid_result.error());
        }
        if (!valid_result.value()) {
            return stl::make_error<sync::AuthToken>("Invalid or expired challenge");
        }
        // Parse public key
        auto key_result = sync::parse_public_key(req.public_key);
        if (!key_result) {
            return stl::make_error<sync::AuthToken>("Invalid public key");
        }
        // Verify signature
        auto verify_result = sync::verify_signature(key_result.value(), req.challenge, req.signature);
        if (!verify_result) {
            return stl::make_error<sync::AuthToken>("{}", verify_result.error());
        }
        if (!verify_result.value()) {
            log::warn("Signature verification failed for key: {}...", req.public_key.substr(0, 30));
            return stl::make_error<sync::AuthToken>("Signature verification failed");
        }
        // Generate token
        std::string token = sync::generate_token();
        i64 expires_at = (sync::now_ms() / 1000) + m_Config.token_expiry;
        // Store token
        auto store_result = m_Meta.store_token(token, expires_at);
        if (!store_result) {
            return stl::make_error<sync::AuthToken>("{}", store_result.error());
        }
        sync::AuthToken resp;
        resp.token = token;
        resp.expires_at = expires_at;
        log::info("Authenticated key: {}...", req.public_key.substr(0, 30));
        return resp;
    }

    stl::result<bool> AuthManager::validate_token(std::string_view token) { return m_Meta.validate_token(token); }

    stl::result<> AuthManager::cleanup_expired() {
        auto r = m_Meta.cleanup_expired_tokens();
        if (!r) {
            return r;
        }
        return stl::success;
    }

    bool AuthManager::is_authorized(std::string_view public_key) {
        std::lock_guard<std::mutex> lock(m_KeysMutex);
        return sync::is_key_authorized(m_AuthorizedKeys, public_key);
    }

} // namespace sap::cloud::auth
