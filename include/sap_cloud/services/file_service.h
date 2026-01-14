#pragma once

#include <sap_cloud/metadata.h>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_fs/fs.h>
#include <sap_sync/sync_types.h>
#include <string>
#include <vector>

namespace sap::cloud::services {

    // Handles generic file storage operations.
    // Coordinates between filesystem (content) and metadata store (index).
    class FileService {
    public:
        FileService(fs::Filesystem& fs, storage::MetadataStore& meta);

        // Get file content
        [[nodiscard]] stl::result<std::vector<u8>> get_file(std::string_view path);

        // Get file metadata
        [[nodiscard]] stl::result<std::optional<sync::FileMetadata>> get_metadata(std::string_view path);

        // Create or update file
        [[nodiscard]] stl::result<sync::FileMetadata> put_file(std::string_view path, const std::vector<u8>& content,
                                                               std::optional<sync::Timestamp> client_mtime = std::nullopt);

        // Delete file
        [[nodiscard]] stl::result<> delete_file(std::string_view path);

        // List all files
        [[nodiscard]] stl::result<std::vector<sync::FileMetadata>> list_files();

        // Get files changed since timestamp
        [[nodiscard]] stl::result<std::vector<sync::FileMetadata>> get_changed_since(sync::Timestamp since);

        // Scan filesystem and update metadata (for initial sync or repair)
        [[nodiscard]] stl::result<size_t> scan_and_index();

    private:
        fs::Filesystem& m_Fs;
        storage::MetadataStore& m_Meta;

        // Build metadata from filesystem
        [[nodiscard]] stl::result<sync::FileMetadata> build_metadata(std::string_view path, const std::vector<u8>& content);
    };

} // namespace sap::cloud::services
