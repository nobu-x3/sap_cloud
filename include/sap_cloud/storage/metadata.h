#pragma once

#include <filesystem>
#include <optional>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_db/database.h>
#include <sap_sync/sync_types.h>
#include <vector>

namespace sap::drive::storage {

    // =============================================================================
    // Metadata Store
    // =============================================================================
    // SQLite-backed storage for file and note metadata.
    // The actual file content is stored on the filesystem; this stores:
    //   - File paths, hashes, sizes, timestamps
    //   - Note titles, tags, full-text search index
    //   - Sync state (for deleted files)
    // =============================================================================

    class MetadataStore {
    public:
        // Open or create the database
        static stl::result<MetadataStore> open(const std::filesystem::path& db_path);
        MetadataStore(const MetadataStore&) = delete;
        MetadataStore& operator=(const MetadataStore&) = delete;
        MetadataStore(MetadataStore&&) noexcept = default;
        MetadataStore& operator=(MetadataStore&&) noexcept = default;

        // Get metadata for a single file
        [[nodiscard]] stl::result<std::optional<sync::FileMetadata>> get_file(std::string_view path);

        // Get all files (optionally changed since timestamp)
        [[nodiscard]] stl::result<std::vector<sync::FileMetadata>> get_all_files(std::optional<sync::Timestamp> since = std::nullopt);

        // Update or insert file metadata
        [[nodiscard]] stl::result<> upsert_file(const sync::FileMetadata& meta);

        // Mark file as deleted (soft delete for sync)
        [[nodiscard]] stl::result<> mark_deleted(std::string_view path);

        // Permanently remove file record
        [[nodiscard]] stl::result<> remove_file(std::string_view path);

        // Get note by ID
        [[nodiscard]] stl::result<std::optional<sync::NoteMetadata>> get_note(std::string_view id);

        // Get note by path
        [[nodiscard]] stl::result<std::optional<sync::NoteMetadata>> get_note_by_path(std::string_view path);

        // Get all notes
        [[nodiscard]] stl::result<std::vector<sync::NoteMetadata>> get_all_notes();

        // Get notes with tag
        [[nodiscard]] stl::result<std::vector<sync::NoteMetadata>> get_notes_by_tag(std::string_view tag);

        // Search notes (full-text)
        [[nodiscard]] stl::result<std::vector<sync::NoteMetadata>> search_notes(std::string_view query);

        // Create or update note
        [[nodiscard]] stl::result<> upsert_note(const sync::NoteMetadata& meta);

        // Delete note
        [[nodiscard]] stl::result<> delete_note(std::string_view id);

        // Get all tags with counts
        [[nodiscard]] stl::result<std::vector<sync::TagInfo>> get_all_tags();

        // Update tags for a note (replaces existing)
        [[nodiscard]] stl::result<> set_note_tags(std::string_view note_id, const std::vector<std::string>& tags);

        // Update FTS index for a note
        [[nodiscard]] stl::result<> update_fts(std::string_view note_id, std::string_view title, std::string_view content);

        // Remove from FTS index
        [[nodiscard]] stl::result<> remove_fts(std::string_view note_id);

        // Store auth token
        [[nodiscard]] stl::result<> store_token(std::string_view token, i64 expires_at);

        // Validate and refresh token
        [[nodiscard]] stl::result<bool> validate_token(std::string_view token);

        // Remove expired tokens
        [[nodiscard]] stl::result<> cleanup_expired_tokens();

        // Store challenge
        [[nodiscard]] stl::result<> store_challenge(std::string_view challenge, std::string_view public_key, i64 expires_at);

        // Validate and consume challenge
        [[nodiscard]] stl::result<bool> validate_challenge(std::string_view challenge, std::string_view public_key);

        // Access underlying database (for transactions)
        [[nodiscard]] db::Database& database() { return m_Db; }

    private:
        explicit MetadataStore(db::Database db);
        stl::result<> init_schema();
        db::Database m_Db;
    };

} // namespace sap::drive::storage
