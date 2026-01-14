#include <sap_cloud/services/file_service.h>
#include <sap_core/log.h>
#include <sap_sync/hash.h>

namespace sap::cloud::services {

    FileService::FileService(fs::Filesystem& fs, storage::MetadataStore& meta) : m_Fs(fs), m_Meta(meta) {}

    stl::result<std::vector<u8>> FileService::get_file(std::string_view path) {
        auto meta_result = m_Meta.get_file(path);
        if (!meta_result) {
            return stl::make_error<std::vector<u8>>("{}", meta_result.error());
        }
        if (!meta_result.value() || meta_result.value()->is_deleted) {
            return stl::make_error<std::vector<u8>>("File not found");
        }
        return m_Fs.read(path);
    }

    stl::result<std::optional<sync::FileMetadata>> FileService::get_metadata(std::string_view path) { return m_Meta.get_file(path); }

    stl::result<sync::FileMetadata> FileService::put_file(std::string_view path, const std::vector<u8>& content,
                                                          std::optional<sync::Timestamp> client_mtime) {
        // Check if file exists (for created_at)
        auto existing_result = m_Meta.get_file(path);
        sync::Timestamp created_at = sync::now_ms();
        if (existing_result && existing_result.value()) {
            created_at = existing_result.value()->created_at;
        }
        // Write to filesystem
        auto write_result = m_Fs.write(path, content);
        if (!write_result) {
            return stl::make_error<sync::FileMetadata>("{}", write_result.error());
        }
        // Set mtime if provided
        if (client_mtime) {
            auto res = m_Fs.set_mtime(path, *client_mtime);
            if (!res)
                return stl::make_error<sync::FileMetadata>("{}", res.error());
        }
        // Build and store metadata
        auto meta_result = build_metadata(path, content);
        if (!meta_result) {
            return stl::make_error<sync::FileMetadata>("{}", meta_result.error());
        }
        auto& meta = meta_result.value();
        meta.created_at = created_at;
        if (client_mtime) {
            meta.mtime = *client_mtime;
        }
        auto store_result = m_Meta.upsert_file(meta);
        if (!store_result) {
            return stl::make_error<sync::FileMetadata>("{}", store_result.error());
        }
        log::debug("Stored file: {} ({} bytes)", path, content.size());
        return meta;
    }

    stl::result<> FileService::delete_file(std::string_view path) {
        // Remove from filesystem
        auto remove_result = m_Fs.remove(path);
        if (!remove_result) {
            log::warn("Failed to remove file from filesystem: {}", remove_result.error());
        }
        // Mark as deleted in metadata (for sync)
        auto mark_result = m_Meta.mark_deleted(path);
        if (!mark_result) {
            return mark_result;
        }
        log::debug("Deleted file: {}", path);
        return stl::success;
    }

    stl::result<std::vector<sync::FileMetadata>> FileService::list_files() { return m_Meta.get_all_files(); }

    stl::result<std::vector<sync::FileMetadata>> FileService::get_changed_since(sync::Timestamp since) {
        return m_Meta.get_all_files(since);
    }

    stl::result<size_t> FileService::scan_and_index() {
        auto files_result = m_Fs.list_recursive();
        if (!files_result) {
            return stl::make_error<size_t>("{}", files_result.error());
        }
        size_t indexed = 0;
        for (const auto& path : files_result.value()) {
            auto content_result = m_Fs.read(path);
            if (!content_result) {
                log::warn("Failed to read file for indexing: {}", path);
                continue;
            }
            auto meta_result = build_metadata(path, content_result.value());
            if (!meta_result) {
                log::warn("Failed to build metadata for: {}", path);
                continue;
            }
            auto store_result = m_Meta.upsert_file(meta_result.value());
            if (!store_result) {
                log::warn("Failed to store metadata for: {}", path);
                continue;
            }
            indexed++;
        }
        log::info("Indexed {} files", indexed);
        return indexed;
    }

    stl::result<sync::FileMetadata> FileService::build_metadata(std::string_view path, const std::vector<u8>& content) {
        sync::FileMetadata meta;
        meta.path = std::string(path);
        meta.hash = sync::hash_bytes(content.data(), content.size());
        meta.size = static_cast<i64>(content.size());
        auto mtime_result = m_Fs.mtime(path);
        meta.mtime = mtime_result ? mtime_result.value() : sync::now_ms();
        meta.created_at = sync::now_ms();
        meta.updated_at = sync::now_ms();
        meta.is_deleted = false;
        return meta;
    }

} // namespace sap::cloud::services
