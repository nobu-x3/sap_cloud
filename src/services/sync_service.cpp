#include "sap_cloud/services/sync_service.h"
#include <sap_sync/sync_types.h>

namespace sap::cloud::services {

    SyncService::SyncService(FileService& fileSvc, NoteService& noteSvc) : m_FileSvc(fileSvc), m_NoteSvc(noteSvc) {}

    stl::result<sync::SyncState> SyncService::get_sync_state(std::optional<sync::Timestamp> since) {
        sync::SyncState state;
        state.server_time = sync::now_ms();
        // Get file metadata
        stl::result<std::vector<sync::FileMetadata>> files_result;
        if (since) {
            files_result = m_FileSvc.get_changed_since(*since);
        } else {
            files_result = m_FileSvc.list_files();
        }
        if (!files_result) {
            return stl::make_error<sync::SyncState>("{}", files_result.error());
        }
        state.files = std::move(files_result.value());
        // Note: Notes are stored as files, so they're already included.
        // If we wanted separate note metadata, we could add it here.
        return state;
    }

} // namespace sap::cloud::services
