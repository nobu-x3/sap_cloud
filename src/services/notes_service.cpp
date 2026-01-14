#include <sap_cloud/services/notes_service.h>
#include <sap_core/log.h>
#include <sap_sync/hash.h>
#include <sap_sync/protocol.h>

namespace sap::cloud::services {

    NoteService::NoteService(fs::Filesystem& fs, storage::MetadataStore& meta) : m_Fs(fs), m_Meta(meta) {}

    std::string NoteService::note_path(std::string_view id) const { return std::string(id) + ".md"; }

    stl::result<std::optional<sync::NoteResponse>> NoteService::get_note(std::string_view id) {
        auto meta_result = m_Meta.get_note(id);
        if (!meta_result) {
            return stl::make_error<std::optional<sync::NoteResponse>>("{}", meta_result.error());
        }
        if (!meta_result.value() || meta_result.value()->is_deleted) {
            return std::optional<sync::NoteResponse>{};
        }
        auto response_result = load_note_response(meta_result.value().value());
        if (!response_result) {
            return stl::make_error<std::optional<sync::NoteResponse>>("{}", response_result.error());
        }
        return {*response_result};
    }

    stl::result<sync::NoteResponse> NoteService::create_note(const sync::NoteCreateRequest& req) {
        // Generate new ID
        std::string id = sync::generate_uuid();
        std::string path = note_path(id);
        // Build content with frontmatter
        sync::ParsedNote parsed;
        parsed.title = req.title;
        parsed.tags = req.tags;
        parsed.content = "# " + req.title + "\n\n" + req.content;
        std::string content = sync::serialize_note(parsed);
        // Write to filesystem
        auto write_result = m_Fs.write(path, content);
        if (!write_result) {
            return stl::make_error<sync::NoteResponse>("{}", write_result.error());
        }
        // Create metadata
        auto now = sync::now_ms();
        sync::NoteMetadata meta;
        meta.id = id;
        meta.path = path;
        meta.title = req.title;
        meta.tags = req.tags;
        meta.hash = sync::hash_string(content);
        meta.created_at = now;
        meta.updated_at = now;
        meta.is_deleted = false;
        // Store metadata
        auto store_result = m_Meta.upsert_note(meta);
        if (!store_result) {
            return stl::make_error<sync::NoteResponse>("{}", store_result.error());
        }
        // Update FTS index
        auto update_res = m_Meta.update_fts(id, req.title, req.content);
        if (!update_res)
            return stl::make_error<sync::NoteResponse>("{}", update_res.error());
        log::debug("Created note: {} ({})", id, req.title);
        // Build response
        sync::NoteResponse resp;
        resp.id = id;
        resp.title = req.title;
        resp.content = req.content;
        resp.tags = req.tags;
        resp.created_at = now;
        resp.updated_at = now;
        return resp;
    }

    stl::result<sync::NoteResponse> NoteService::update_note(std::string_view id, const sync::NoteUpdateRequest& req) {
        // Get existing note
        auto existing_result = m_Meta.get_note(id);
        if (!existing_result) {
            return stl::make_error<sync::NoteResponse>("{}", existing_result.error());
        }
        if (!existing_result.value() || existing_result.value()->is_deleted) {
            return stl::make_error<sync::NoteResponse>("{}", "Note not found");
        }
        auto& existing = existing_result.value().value();
        // Load current content
        auto content_result = m_Fs.read_string(existing.path);
        if (!content_result) {
            return stl::make_error<sync::NoteResponse>("{}", content_result.error());
        }
        // Parse existing content
        auto parse_result = sync::parse_note(content_result.value());
        if (!parse_result) {
            return stl::make_error<sync::NoteResponse>("{}", parse_result.error());
        }
        auto& parsed = parse_result.value();
        // Apply updates
        std::string new_title = req.title.value_or(existing.title);
        std::vector<std::string> new_tags = req.tags.value_or(existing.tags);
        std::string new_content = req.content.value_or(parsed.content);
        // Rebuild content
        parsed.title = new_title;
        parsed.tags = new_tags;
        parsed.content = new_content;
        std::string serialized = sync::serialize_note(parsed);
        // Write to filesystem
        auto write_result = m_Fs.write(existing.path, serialized);
        if (!write_result) {
            return stl::make_error<sync::NoteResponse>("{}", write_result.error());
        }
        // Update metadata
        auto now = sync::now_ms();
        sync::NoteMetadata meta;
        meta.id = std::string(id);
        meta.path = existing.path;
        meta.title = new_title;
        meta.tags = new_tags;
        meta.hash = sync::hash_string(serialized);
        meta.created_at = existing.created_at;
        meta.updated_at = now;
        meta.is_deleted = false;
        auto store_result = m_Meta.upsert_note(meta);
        if (!store_result) {
            return stl::make_error<sync::NoteResponse>("{}", store_result.error());
        }
        // Update FTS
        auto update_res = m_Meta.update_fts(std::string(id), new_title, new_content);
        if (!update_res)
            return stl::make_error<sync::NoteResponse>("{}", update_res.error());
        log::debug("Updated note: {} ({})", id, new_title);
        // Build response
        sync::NoteResponse resp;
        resp.id = std::string(id);
        resp.title = new_title;
        resp.content = new_content;
        resp.tags = new_tags;
        resp.created_at = existing.created_at;
        resp.updated_at = now;
        return resp;
    }

    stl::result<> NoteService::delete_note(std::string_view id) {
        auto meta_result = m_Meta.get_note(id);
        if (!meta_result) {
            return stl::make_error("{}", meta_result.error());
        }
        if (!meta_result.value()) {
            return stl::make_error("Note not found");
        }
        // Remove from filesystem
        auto remove_result = m_Fs.remove(meta_result.value()->path);
        if (!remove_result)
            return remove_result;
        // Mark as deleted
        auto delete_result = m_Meta.delete_note(id);
        if (!delete_result) {
            return delete_result;
        }
        log::debug("Deleted note: {}", id);
        return stl::success;
    }

    stl::result<sync::NoteListResponse> NoteService::list_notes(const ListOptions& options) {
        std::vector<sync::NoteMetadata> notes;
        if (options.search) {
            auto search_result = m_Meta.search_notes(*options.search);
            if (!search_result) {
                return stl::make_error<sync::NoteListResponse>("{}", search_result.error());
            }
            notes = std::move(search_result.value());
        } else if (options.tag) {
            auto tag_result = m_Meta.get_notes_by_tag(*options.tag);
            if (!tag_result) {
                return stl::make_error<sync::NoteListResponse>("{}", tag_result.error());
            }
            notes = std::move(tag_result.value());
        } else {
            auto all_result = m_Meta.get_all_notes();
            if (!all_result) {
                return stl::make_error<sync::NoteListResponse>("{}", all_result.error());
            }
            notes = std::move(all_result.value());
        }
        sync::NoteListResponse resp;
        resp.total = static_cast<i64>(notes.size());
        // Apply pagination
        size_t start = static_cast<size_t>(options.offset);
        size_t end = std::min(start + static_cast<size_t>(options.limit), notes.size());
        for (size_t i = start; i < end; ++i) {
            auto& meta = notes[i];
            // Load content for preview
            auto content_result = m_Fs.read_string(meta.path);
            std::string content = content_result ? content_result.value() : "";
            resp.notes.push_back(to_list_item(meta, content));
        }
        return resp;
    }

    stl::result<sync::TagListResponse> NoteService::get_tags() {
        auto tags_result = m_Meta.get_all_tags();
        if (!tags_result) {
            return stl::make_error<sync::TagListResponse>("{}", tags_result.error());
        }
        sync::TagListResponse resp;
        resp.tags = std::move(tags_result.value());
        return resp;
    }

    stl::result<sync::NoteListResponse> NoteService::get_notes_by_tag(std::string_view tag) {
        ListOptions options;
        options.tag = std::string(tag);
        return list_notes(options);
    }

    stl::result<sync::NoteListResponse> NoteService::search_notes(std::string_view query) {
        ListOptions options;
        options.search = std::string(query);
        return list_notes(options);
    }

    stl::result<std::optional<sync::NoteMetadata>> NoteService::get_metadata(std::string_view id) { return m_Meta.get_note(id); }

    stl::result<std::vector<sync::NoteMetadata>> NoteService::get_all_metadata() { return m_Meta.get_all_notes(); }

    stl::result<size_t> NoteService::scan_and_index() {
        auto files_result = m_Fs.list_recursive();
        if (!files_result) {
            return stl::make_error<size_t>("{}", files_result.error());
        }
        size_t indexed = 0;
        for (const auto& path : files_result.value()) {
            // Only process .md files
            if (path.size() < 3 || path.substr(path.size() - 3) != ".md") {
                continue;
            }
            auto content_result = m_Fs.read_string(path);
            if (!content_result) {
                log::warn("Failed to read note: {}", path);
                continue;
            }
            auto parse_result = sync::parse_note(content_result.value());
            if (!parse_result) {
                log::warn("Failed to parse note: {}", path);
                continue;
            }
            auto& parsed = parse_result.value();
            // Extract ID from path (remove .md extension)
            std::string id = path.substr(0, path.size() - 3);
            // Check if already exists
            auto existing_result = m_Meta.get_note(id);
            sync::Timestamp created_at = sync::now_ms();
            if (existing_result && existing_result.value()) {
                created_at = existing_result.value()->created_at;
            }
            // Build metadata
            sync::NoteMetadata meta;
            meta.id = id;
            meta.path = path;
            meta.title = parsed.title;
            meta.tags = parsed.tags;
            meta.hash = sync::hash_string(content_result.value());
            meta.created_at = created_at;
            meta.updated_at = sync::now_ms();
            meta.is_deleted = false;
            auto store_result = m_Meta.upsert_note(meta);
            if (!store_result) {
                log::warn("Failed to store note metadata: {}", path);
                continue;
            }
            // Update FTS
            auto update_result = m_Meta.update_fts(id, parsed.title, parsed.content);
            if(!update_result)
                return stl::make_error<size_t>("{}", update_result.error());
            indexed++;
        }
        log::info("Indexed {} notes", indexed);
        return indexed;
    }

    stl::result<sync::NoteResponse> NoteService::load_note_response(const sync::NoteMetadata& meta) {
        auto content_result = m_Fs.read_string(meta.path);
        if (!content_result) {
            return stl::make_error<sync::NoteResponse>("{}", content_result.error());
        }
        auto parse_result = sync::parse_note(content_result.value());
        if (!parse_result) {
            return stl::make_error<sync::NoteResponse>("{}", parse_result.error());
        }
        sync::NoteResponse resp;
        resp.id = meta.id;
        resp.title = meta.title;
        resp.content = parse_result.value().content;
        resp.tags = meta.tags;
        resp.created_at = meta.created_at;
        resp.updated_at = meta.updated_at;
        return resp;
    }

    sync::NoteListItem NoteService::to_list_item(const sync::NoteMetadata& meta, std::string_view content) {
        sync::NoteListItem item;
        item.id = meta.id;
        item.title = meta.title;
        item.tags = meta.tags;
        item.updated_at = meta.updated_at;
        item.preview = sync::generate_preview(content);
        return item;
    }

} // namespace sap::cloud::services
