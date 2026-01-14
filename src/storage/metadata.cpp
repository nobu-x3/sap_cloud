#include "sap_cloud/storage/metadata.h"
#include <sap_core/log.h>
#include <sstream>

namespace sap::cloud::storage {

    MetadataStore::MetadataStore(db::Database db) : m_Db(std::move(db)) {}

    stl::result<MetadataStore> MetadataStore::open(const std::filesystem::path& db_path) {
        auto db_result = db::Database::open(db_path);
        if (!db_result) {
            return stl::make_error<MetadataStore>("Failed to open database: {}", db_result.error());
        }
        MetadataStore store(std::move(db_result.value()));
        auto init_result = store.init_schema();
        if (!init_result) {
            return stl::make_error<MetadataStore>("{}", init_result.error());
        }
        return store;
    }

    stl::result<> MetadataStore::init_schema() {
        // Files table
        auto r1 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS files (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            path        TEXT NOT NULL UNIQUE,
            hash        TEXT NOT NULL,
            size        INTEGER NOT NULL,
            mtime       INTEGER NOT NULL,
            created_at  INTEGER NOT NULL,
            updated_at  INTEGER NOT NULL,
            is_deleted  INTEGER DEFAULT 0
        )
    )");
        if (!r1)
            return r1;
        // Notes table
        auto r2 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS notes (
            id          TEXT PRIMARY KEY,
            path        TEXT NOT NULL UNIQUE,
            title       TEXT NOT NULL,
            hash        TEXT NOT NULL,
            created_at  INTEGER NOT NULL,
            updated_at  INTEGER NOT NULL,
            is_deleted  INTEGER DEFAULT 0
        )
    )");
        if (!r2)
            return r2;
        // Tags table
        auto r3 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS tags (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            name    TEXT NOT NULL UNIQUE
        )
    )");
        if (!r3)
            return r3;
        // Note-tag junction table
        auto r4 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS note_tags (
            note_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            tag_id  INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
            PRIMARY KEY (note_id, tag_id)
        )
    )");
        if (!r4)
            return r4;
        // Full-text search
        auto r5 = m_Db.execute(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
            note_id,
            title,
            content,
            tokenize='porter unicode61'
        )
    )");
        if (!r5)
            return r5;
        // Auth tokens
        auto r6 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS auth_tokens (
            token       TEXT PRIMARY KEY,
            created_at  INTEGER NOT NULL,
            expires_at  INTEGER NOT NULL,
            last_used   INTEGER
        )
    )");
        if (!r6)
            return r6;
        // Auth challenges
        auto r7 = m_Db.execute(R"(
        CREATE TABLE IF NOT EXISTS auth_challenges (
            challenge   TEXT PRIMARY KEY,
            public_key  TEXT NOT NULL,
            expires_at  INTEGER NOT NULL
        )
    )");
        if (!r7)
            return r7;
        // Indexes
        m_Db.execute("CREATE INDEX IF NOT EXISTS idx_files_path ON files(path)");
        m_Db.execute("CREATE INDEX IF NOT EXISTS idx_files_updated ON files(updated_at)");
        m_Db.execute("CREATE INDEX IF NOT EXISTS idx_notes_path ON notes(path)");
        m_Db.execute("CREATE INDEX IF NOT EXISTS idx_note_tags_note ON note_tags(note_id)");
        m_Db.execute("CREATE INDEX IF NOT EXISTS idx_note_tags_tag ON note_tags(tag_id)");
        log::debug("Database schema initialized");
        return stl::success;
    }

    stl::result<std::optional<sync::FileMetadata>> MetadataStore::get_file(std::string_view path) {
        auto stmt = m_Db.prepare("SELECT path, hash, size, mtime, created_at, updated_at, is_deleted "
                                 "FROM files WHERE path = ?");
        if (!stmt)
            return stl::make_error<std::optional<sync::FileMetadata>>("{}", stmt.error());
        stmt->bind(1, path);
        auto row = stmt->fetch_one();
        if (!row)
            return stl::make_error<std::optional<sync::FileMetadata>>("{}", row.error());
        if (!row.value())
            return std::optional<sync::FileMetadata>{};
        sync::FileMetadata meta;
        meta.path = row.value()->get<std::string>("path");
        meta.hash = row.value()->get<std::string>("hash");
        meta.size = row.value()->get<i64>("size");
        meta.mtime = row.value()->get<i64>("mtime");
        meta.created_at = row.value()->get<i64>("created_at");
        meta.updated_at = row.value()->get<i64>("updated_at");
        meta.is_deleted = row.value()->get<i64>("is_deleted") != 0;
        return {meta};
    }

    stl::result<std::vector<sync::FileMetadata>> MetadataStore::get_all_files(std::optional<sync::Timestamp> since) {
        std::string sql = "SELECT path, hash, size, mtime, created_at, updated_at, is_deleted FROM files";
        if (since) {
            sql += " WHERE updated_at > ?";
        }
        auto stmt = m_Db.prepare(sql);
        if (!stmt)
            return stl::make_error<std::vector<sync::FileMetadata>>("{}", stmt.error());
        if (since) {
            stmt->bind(1, *since);
        }
        auto rows = stmt->fetch_all();
        if (!rows)
            return stl::make_error<std::vector<sync::FileMetadata>>("{}", rows.error());
        std::vector<sync::FileMetadata> files;
        for (const auto& row : rows.value()) {
            sync::FileMetadata meta;
            meta.path = row.get<std::string>("path");
            meta.hash = row.get<std::string>("hash");
            meta.size = row.get<i64>("size");
            meta.mtime = row.get<i64>("mtime");
            meta.created_at = row.get<i64>("created_at");
            meta.updated_at = row.get<i64>("updated_at");
            meta.is_deleted = row.get<i64>("is_deleted") != 0;
            files.push_back(std::move(meta));
        }
        return files;
    }

    stl::result<> MetadataStore::upsert_file(const sync::FileMetadata& meta) {
        auto stmt = m_Db.prepare(R"(
        INSERT INTO files (path, hash, size, mtime, created_at, updated_at, is_deleted)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            hash = excluded.hash,
            size = excluded.size,
            mtime = excluded.mtime,
            updated_at = excluded.updated_at,
            is_deleted = excluded.is_deleted
    )");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, meta.path);
        stmt->bind(2, meta.hash);
        stmt->bind(3, meta.size);
        stmt->bind(4, meta.mtime);
        stmt->bind(5, meta.created_at);
        stmt->bind(6, meta.updated_at);
        stmt->bind(7, static_cast<i64>(meta.is_deleted ? 1LL : 0LL));
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<> MetadataStore::mark_deleted(std::string_view path) {
        auto now = sync::now_ms();
        auto stmt = m_Db.prepare("UPDATE files SET is_deleted = 1, updated_at = ? WHERE path = ?");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, now);
        stmt->bind(2, path);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<> MetadataStore::remove_file(std::string_view path) {
        auto stmt = m_Db.prepare("DELETE FROM files WHERE path = ?");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, path);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<std::optional<sync::NoteMetadata>> MetadataStore::get_note(std::string_view id) {
        auto stmt = m_Db.prepare(R"(
        SELECT n.id, n.path, n.title, n.hash, n.created_at, n.updated_at, n.is_deleted,
               GROUP_CONCAT(t.name) as tags
        FROM notes n
        LEFT JOIN note_tags nt ON n.id = nt.note_id
        LEFT JOIN tags t ON nt.tag_id = t.id
        WHERE n.id = ?
        GROUP BY n.id
    )");
        if (!stmt)
            return stl::make_error<std::optional<sync::NoteMetadata>>("{}", stmt.error());
        stmt->bind(1, id);
        auto row = stmt->fetch_one();
        if (!row)
            return stl::make_error<std::optional<sync::NoteMetadata>>("{}", row.error());
        if (!row.value())
            return std::optional<sync::NoteMetadata>{};
        sync::NoteMetadata meta;
        meta.id = row.value()->get<std::string>("id");
        meta.path = row.value()->get<std::string>("path");
        meta.title = row.value()->get<std::string>("title");
        meta.hash = row.value()->get<std::string>("hash");
        meta.created_at = row.value()->get<i64>("created_at");
        meta.updated_at = row.value()->get<i64>("updated_at");
        meta.is_deleted = row.value()->get<i64>("is_deleted") != 0;
        // Parse tags
        auto tags_opt = row.value()->try_get<std::string>("tags");
        if (tags_opt && !tags_opt->empty()) {
            std::istringstream iss(*tags_opt);
            std::string tag;
            while (std::getline(iss, tag, ',')) {
                meta.tags.push_back(tag);
            }
        }
        return {meta};
    }

    stl::result<std::optional<sync::NoteMetadata>> MetadataStore::get_note_by_path(std::string_view path) {
        auto stmt = m_Db.prepare("SELECT id FROM notes WHERE path = ?");
        if (!stmt)
            return stl::make_error<std::optional<sync::NoteMetadata>>("{}", stmt.error());
        stmt->bind(1, path);
        auto row = stmt->fetch_one();
        if (!row)
            return stl::make_error<std::optional<sync::NoteMetadata>>("{}", row.error());
        if (!row.value())
            return std::optional<sync::NoteMetadata>{};
        return get_note(row.value()->get<std::string>("id"));
    }

    stl::result<std::vector<sync::NoteMetadata>> MetadataStore::get_all_notes() {
        auto rows = m_Db.query(R"(
        SELECT n.id, n.path, n.title, n.hash, n.created_at, n.updated_at, n.is_deleted,
               GROUP_CONCAT(t.name) as tags
        FROM notes n
        LEFT JOIN note_tags nt ON n.id = nt.note_id
        LEFT JOIN tags t ON nt.tag_id = t.id
        WHERE n.is_deleted = 0
        GROUP BY n.id
        ORDER BY n.updated_at DESC
    )");
        if (!rows)
            return stl::make_error<std::vector<sync::NoteMetadata>>("{}", rows.error());
        std::vector<sync::NoteMetadata> notes;
        for (const auto& row : rows.value()) {
            sync::NoteMetadata meta;
            meta.id = row.get<std::string>("id");
            meta.path = row.get<std::string>("path");
            meta.title = row.get<std::string>("title");
            meta.hash = row.get<std::string>("hash");
            meta.created_at = row.get<i64>("created_at");
            meta.updated_at = row.get<i64>("updated_at");
            meta.is_deleted = row.get<i64>("is_deleted") != 0;
            auto tags_opt = row.try_get<std::string>("tags");
            if (tags_opt && !tags_opt->empty()) {
                std::istringstream iss(*tags_opt);
                std::string tag;
                while (std::getline(iss, tag, ',')) {
                    meta.tags.push_back(tag);
                }
            }
            notes.push_back(std::move(meta));
        }
        return notes;
    }

    stl::result<std::vector<sync::NoteMetadata>> MetadataStore::get_notes_by_tag(std::string_view tag) {
        auto stmt = m_Db.prepare(R"(
        SELECT n.id, n.path, n.title, n.hash, n.created_at, n.updated_at, n.is_deleted,
               GROUP_CONCAT(t2.name) as tags
        FROM notes n
        JOIN note_tags nt ON n.id = nt.note_id
        JOIN tags t ON nt.tag_id = t.id
        LEFT JOIN note_tags nt2 ON n.id = nt2.note_id
        LEFT JOIN tags t2 ON nt2.tag_id = t2.id
        WHERE t.name = ? AND n.is_deleted = 0
        GROUP BY n.id
        ORDER BY n.updated_at DESC
    )");
        if (!stmt)
            return stl::make_error<std::vector<sync::NoteMetadata>>("{}", stmt.error());
        stmt->bind(1, tag);
        auto rows = stmt->fetch_all();
        if (!rows)
            return stl::make_error<std::vector<sync::NoteMetadata>>("{}", rows.error());
        std::vector<sync::NoteMetadata> notes;
        for (const auto& row : rows.value()) {
            sync::NoteMetadata meta;
            meta.id = row.get<std::string>("id");
            meta.path = row.get<std::string>("path");
            meta.title = row.get<std::string>("title");
            meta.hash = row.get<std::string>("hash");
            meta.created_at = row.get<i64>("created_at");
            meta.updated_at = row.get<i64>("updated_at");
            meta.is_deleted = false;
            auto tags_opt = row.try_get<std::string>("tags");
            if (tags_opt && !tags_opt->empty()) {
                std::istringstream iss(*tags_opt);
                std::string t;
                while (std::getline(iss, t, ',')) {
                    meta.tags.push_back(t);
                }
            }
            notes.push_back(std::move(meta));
        }
        return notes;
    }

    stl::result<std::vector<sync::NoteMetadata>> MetadataStore::search_notes(std::string_view query) {
        auto stmt = m_Db.prepare(R"(
        SELECT n.id, n.path, n.title, n.hash, n.created_at, n.updated_at, n.is_deleted,
               GROUP_CONCAT(t.name) as tags
        FROM notes n
        JOIN notes_fts fts ON n.id = fts.note_id
        LEFT JOIN note_tags nt ON n.id = nt.note_id
        LEFT JOIN tags t ON nt.tag_id = t.id
        WHERE notes_fts MATCH ? AND n.is_deleted = 0
        GROUP BY n.id
        ORDER BY rank
    )");
        if (!stmt)
            return stl::make_error<std::vector<sync::NoteMetadata>>("{}", stmt.error());
        stmt->bind(1, query);
        auto rows = stmt->fetch_all();
        if (!rows)
            return stl::make_error<std::vector<sync::NoteMetadata>>("{}", rows.error());
        std::vector<sync::NoteMetadata> notes;
        for (const auto& row : rows.value()) {
            sync::NoteMetadata meta;
            meta.id = row.get<std::string>("id");
            meta.path = row.get<std::string>("path");
            meta.title = row.get<std::string>("title");
            meta.hash = row.get<std::string>("hash");
            meta.created_at = row.get<i64>("created_at");
            meta.updated_at = row.get<i64>("updated_at");
            meta.is_deleted = false;
            auto tags_opt = row.try_get<std::string>("tags");
            if (tags_opt && !tags_opt->empty()) {
                std::istringstream iss(*tags_opt);
                std::string t;
                while (std::getline(iss, t, ',')) {
                    meta.tags.push_back(t);
                }
            }
            notes.push_back(std::move(meta));
        }
        return notes;
    }

    stl::result<> MetadataStore::upsert_note(const sync::NoteMetadata& meta) {
        auto stmt = m_Db.prepare(R"(
        INSERT INTO notes (id, path, title, hash, created_at, updated_at, is_deleted)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            path = excluded.path,
            title = excluded.title,
            hash = excluded.hash,
            updated_at = excluded.updated_at,
            is_deleted = excluded.is_deleted
    )");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, meta.id);
        stmt->bind(2, meta.path);
        stmt->bind(3, meta.title);
        stmt->bind(4, meta.hash);
        stmt->bind(5, meta.created_at);
        stmt->bind(6, meta.updated_at);
        stmt->bind(7, static_cast<i64>(meta.is_deleted ? 1LL : 0LL));
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        // Update tags
        auto tag_result = set_note_tags(meta.id, meta.tags);
        if (!tag_result)
            return tag_result;
        return stl::success;
    }

    stl::result<> MetadataStore::delete_note(std::string_view id) {
        auto now = sync::now_ms();
        auto stmt = m_Db.prepare("UPDATE notes SET is_deleted = 1, updated_at = ? WHERE id = ?");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, now);
        stmt->bind(2, id);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        // Remove from FTS
        auto res = remove_fts(id);
        if (!res)
            return res;
        return stl::success;
    }

    stl::result<std::vector<sync::TagInfo>> MetadataStore::get_all_tags() {
        auto rows = m_Db.query(R"(
        SELECT t.name, COUNT(nt.note_id) as count
        FROM tags t
        LEFT JOIN note_tags nt ON t.id = nt.tag_id
        LEFT JOIN notes n ON nt.note_id = n.id AND n.is_deleted = 0
        GROUP BY t.id
        HAVING count > 0
        ORDER BY count DESC, t.name
    )");
        if (!rows)
            return stl::make_error<std::vector<sync::TagInfo>>("{}", rows.error());
        std::vector<sync::TagInfo> tags;
        for (const auto& row : rows.value()) {
            sync::TagInfo info;
            info.name = row.get<std::string>("name");
            info.count = row.get<i64>("count");
            tags.push_back(std::move(info));
        }
        return tags;
    }

    stl::result<> MetadataStore::set_note_tags(std::string_view note_id, const std::vector<std::string>& tags) {
        // Remove existing tags
        auto del_stmt = m_Db.prepare("DELETE FROM note_tags WHERE note_id = ?");
        if (!del_stmt)
            return stl::make_error("{}", del_stmt.error());
        del_stmt->bind(1, note_id);
        del_stmt->execute();
        // Add new tags
        for (const auto& tag : tags) {
            // Ensure tag exists
            auto insert_tag = m_Db.prepare("INSERT OR IGNORE INTO tags (name) VALUES (?)");
            if (insert_tag) {
                insert_tag->bind(1, tag);
                insert_tag->execute();
            }
            // Get tag ID
            auto get_tag = m_Db.prepare("SELECT id FROM tags WHERE name = ?");
            if (!get_tag)
                continue;
            get_tag->bind(1, tag);
            auto tag_row = get_tag->fetch_one();
            if (!tag_row || !tag_row.value())
                continue;
            i64 tag_id = tag_row.value()->get<i64>("id");
            // Link note to tag
            auto link = m_Db.prepare("INSERT OR IGNORE INTO note_tags (note_id, tag_id) VALUES (?, ?)");
            if (link) {
                link->bind(1, note_id);
                link->bind(2, tag_id);
                link->execute();
            }
        }
        return stl::success;
    }

    stl::result<> MetadataStore::update_fts(std::string_view note_id, std::string_view title, std::string_view content) {
        // Remove existing entry
        auto res = remove_fts(note_id);
        if (!res)
            return res;
        // Insert new entry
        auto stmt = m_Db.prepare("INSERT INTO notes_fts (note_id, title, content) VALUES (?, ?, ?)");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, note_id);
        stmt->bind(2, title);
        stmt->bind(3, content);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<> MetadataStore::remove_fts(std::string_view note_id) {
        auto stmt = m_Db.prepare("DELETE FROM notes_fts WHERE note_id = ?");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, note_id);
        stmt->execute();
        return stl::success;
    }

    stl::result<> MetadataStore::store_token(std::string_view token, i64 expires_at) {
        auto now = sync::now_ms() / 1000; // Seconds
        auto stmt = m_Db.prepare("INSERT INTO auth_tokens (token, created_at, expires_at) VALUES (?, ?, ?)");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, token);
        stmt->bind(2, now);
        stmt->bind(3, expires_at);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<bool> MetadataStore::validate_token(std::string_view token) {
        auto now = sync::now_ms() / 1000;
        auto stmt = m_Db.prepare("SELECT 1 FROM auth_tokens WHERE token = ? AND expires_at > ?");
        if (!stmt)
            return stl::make_error<bool>("{}", stmt.error());
        stmt->bind(1, token);
        stmt->bind(2, now);
        auto row = stmt->fetch_one();
        if (!row)
            return stl::make_error<bool>("{}", row.error());
        if (!row.value())
            return false;
        // Update last_used
        auto update = m_Db.prepare("UPDATE auth_tokens SET last_used = ? WHERE token = ?");
        if (update) {
            update->bind(1, now);
            update->bind(2, token);
            update->execute();
        }
        return true;
    }

    stl::result<> MetadataStore::cleanup_expired_tokens() {
        auto now = sync::now_ms() / 1000;
        auto stmt = m_Db.prepare("DELETE FROM auth_tokens WHERE expires_at < ?");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, now);
        stmt->execute();
        return stl::success;
    }

    stl::result<> MetadataStore::store_challenge(std::string_view challenge, std::string_view public_key, i64 expires_at) {
        auto stmt = m_Db.prepare("INSERT INTO auth_challenges (challenge, public_key, expires_at) VALUES (?, ?, ?)");
        if (!stmt)
            return stl::make_error("{}", stmt.error());
        stmt->bind(1, challenge);
        stmt->bind(2, public_key);
        stmt->bind(3, expires_at);
        auto r = stmt->execute();
        if (!r)
            return stl::make_error("{}", r.error());
        return stl::success;
    }

    stl::result<bool> MetadataStore::validate_challenge(std::string_view challenge, std::string_view public_key) {
        auto now = sync::now_ms() / 1000;
        auto stmt = m_Db.prepare(R"(
        SELECT 1 FROM auth_challenges 
        WHERE challenge = ? AND public_key = ? AND expires_at > ?
    )");
        if (!stmt)
            return stl::make_error<bool>("{}", stmt.error());
        stmt->bind(1, challenge);
        stmt->bind(2, public_key);
        stmt->bind(3, now);
        auto row = stmt->fetch_one();
        if (!row)
            return stl::make_error<bool>("{}", row.error());
        if (!row.value())
            return false;
        // Consume the challenge (one-time use)
        auto del = m_Db.prepare("DELETE FROM auth_challenges WHERE challenge = ?");
        if (del) {
            del->bind(1, challenge);
            del->execute();
        }
        return true;
    }

} // namespace sap::cloud::storage
