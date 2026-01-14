#pragma once

#include <optional>
#include <sap_cloud/storage/metadata.h>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_sync/sync_types.h>
#include <sap_fs/fs.h>
#include <string>
#include <vector>

namespace sap::cloud::services {

    // Handles markdown note operations.
    // Notes are stored as .md files with YAML frontmatter for tags.
    class NoteService {
    public:
        NoteService(fs::Filesystem& fs, storage::MetadataStore& meta);
        // CRUD Operations

        // Get note by ID
        [[nodiscard]] stl::result<std::optional<sync::NoteResponse>> get_note(std::string_view id);

        // Create new note
        [[nodiscard]] stl::result<sync::NoteResponse> create_note(const sync::NoteCreateRequest& req);

        // Update existing note
        [[nodiscard]] stl::result<sync::NoteResponse> update_note(std::string_view id, const sync::NoteUpdateRequest& req);

        // Delete note
        [[nodiscard]] stl::result<> delete_note(std::string_view id);

        // List notes with optional filters
        struct ListOptions {
            std::optional<std::string> tag;
            std::optional<std::string> search;
            i64 limit = 50;
            i64 offset = 0;
        };

        [[nodiscard]] stl::result<sync::NoteListResponse> list_notes(const ListOptions& options);

        // Get all tags
        [[nodiscard]] stl::result<sync::TagListResponse> get_tags();

        // Get notes by tag
        [[nodiscard]] stl::result<sync::NoteListResponse> get_notes_by_tag(std::string_view tag);

        // Search notes
        [[nodiscard]] stl::result<sync::NoteListResponse> search_notes(std::string_view query);

        // Get note metadata (for sync)
        [[nodiscard]] stl::result<std::optional<sync::NoteMetadata>> get_metadata(std::string_view id);

        // Get all note metadata
        [[nodiscard]] stl::result<std::vector<sync::NoteMetadata>> get_all_metadata();

        // Scan filesystem and rebuild index
        [[nodiscard]] stl::result<size_t> scan_and_index();

    private:
        fs::Filesystem& m_Fs;
        storage::MetadataStore& m_Meta;

        // Helper to convert NoteMetadata + content to NoteResponse
        [[nodiscard]] stl::result<sync::NoteResponse> load_note_response(const sync::NoteMetadata& meta);

        // Helper to convert NoteMetadata to NoteListItem
        [[nodiscard]] sync::NoteListItem to_list_item(const sync::NoteMetadata& meta, std::string_view content);

        // Generate file path for note ID
        [[nodiscard]] std::string note_path(std::string_view id) const;
    };

} // namespace sap::cloud::services
