#pragma once

#include <optional>
#include <sap_cloud/services/file_service.h>
#include <sap_cloud/services/notes_service.h>
#include <sap_core/result.h>
#include <sap_core/types.h>
#include <sap_sync/sync_types.h>

namespace sap::cloud::services {

    // Handles sync state requests for clients.
    // Combines file and note metadata into unified sync responses.
    class SyncService {
    public:
        SyncService(FileService& fileSvc, NoteService& noteSvc);

        // Get sync state (all files, or changed since timestamp)
        [[nodiscard]] stl::result<sync::SyncState> get_sync_state(std::optional<sync::Timestamp> since = std::nullopt);

    private:
        FileService& m_FileSvc;
        NoteService& m_NoteSvc;
    };

} // namespace sap::cloud::services
