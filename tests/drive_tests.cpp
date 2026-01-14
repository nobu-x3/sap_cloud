#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sap_cloud/services/file_service.h>
#include <sap_cloud/storage/metadata.h>
#include <sap_cloud/config.h>
#include <sap_fs/fs.h>
#include <sap_sync/sync_types.h>

using namespace sap;
using namespace sap::cloud;

namespace sfs = std::filesystem;

class FilesystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_TestDir = sfs::temp_directory_path() / "sap_drive_test";
        sfs::create_directories(m_TestDir);
        m_Fs = std::make_unique<sap::fs::Filesystem>(m_TestDir);
    }
    void TearDown() override { sfs::remove_all(m_TestDir); }
    sfs::path m_TestDir;
    std::unique_ptr<sap::fs::Filesystem> m_Fs;
};

class MetadataStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_DbPath = sfs::temp_directory_path() / "sap_drive_test.db";
        auto result = storage::MetadataStore::open(m_DbPath);
        ASSERT_TRUE(result.has_value()) << result.error();
        m_Store = std::make_unique<storage::MetadataStore>(std::move(result.value()));
    }
    void TearDown() override {
        m_Store.reset();
        sfs::remove(m_DbPath);
    }
    sfs::path m_DbPath;
    std::unique_ptr<storage::MetadataStore> m_Store;
};

class FileServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_TestDir = sfs::temp_directory_path() / "sap_drive_svc_test";
        m_DbPath = m_TestDir / "test.db";
        sfs::create_directories(m_TestDir);
        m_Fs = std::make_unique<fs::Filesystem>(m_TestDir / "files");
        sfs::create_directories(m_TestDir / "files");
        auto store_result = storage::MetadataStore::open(m_DbPath);
        ASSERT_TRUE(store_result.has_value());
        m_Store = std::make_unique<storage::MetadataStore>(std::move(store_result.value()));
        m_Service = std::make_unique<services::FileService>(*m_Fs, *m_Store);
    }
    void TearDown() override {
        m_Service.reset();
        m_Store.reset();
        m_Fs.reset();
        sfs::remove_all(m_TestDir);
    }
    sfs::path m_TestDir;
    sfs::path m_DbPath;
    std::unique_ptr<fs::Filesystem> m_Fs;
    std::unique_ptr<storage::MetadataStore> m_Store;
    std::unique_ptr<services::FileService> m_Service;
};

TEST_F(FilesystemTest, WriteAndRead) {
    std::vector<u8> content = {'H', 'e', 'l', 'l', 'o'};
    auto write_result = m_Fs->write("test.txt", content);
    ASSERT_TRUE(write_result.has_value()) << write_result.error();
    auto readResult = m_Fs->read("test.txt");
    ASSERT_TRUE(readResult.has_value()) << readResult.error();
    EXPECT_EQ(readResult.value(), content);
}

TEST_F(FilesystemTest, WriteCreatesParentDirs) {
    std::vector<u8> content = {'T', 'e', 's', 't'};
    auto result = m_Fs->write("a/b/c/deep.txt", content);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(m_Fs->exists("a/b/c/deep.txt"));
}

TEST_F(FilesystemTest, ReadString) {
    auto write_result = m_Fs->write("text.txt", "Hello, World!");
    ASSERT_TRUE(write_result.has_value());
    auto readResult = m_Fs->read_string("text.txt");
    ASSERT_TRUE(readResult.has_value());
    EXPECT_EQ(readResult.value(), "Hello, World!");
}

TEST_F(FilesystemTest, FileNotFound) {
    auto result = m_Fs->read("nonexistent.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FilesystemTest, PathTraversalBlocked) {
    auto result = m_Fs->write("../escape.txt", "bad");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("escape") != std::string::npos);
}

TEST_F(FilesystemTest, Remove) {
    auto res = m_Fs->write("to_delete.txt", "temp");
    EXPECT_TRUE(m_Fs->exists("to_delete.txt"));
    auto result = m_Fs->remove("to_delete.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(m_Fs->exists("to_delete.txt"));
}

TEST_F(FilesystemTest, ListDirectory) {
    auto res = m_Fs->write("file1.txt", "1");
    res = m_Fs->write("file2.txt", "2");
    res = m_Fs->write("subdir/file3.txt", "3");
    auto result = m_Fs->list();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result.value().size(), 2); // At least file1, file2
}

TEST_F(FilesystemTest, ListRecursive) {
    auto res = m_Fs->write("file1.txt", "1");
    res = m_Fs->write("a/file2.txt", "2");
    res = m_Fs->write("a/b/file3.txt", "3");
    auto result = m_Fs->list_recursive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

TEST_F(FilesystemTest, Size) {
    std::string content = "12345";
    auto res = m_Fs->write("sized.txt", content);
    auto result = m_Fs->size("sized.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);
}

TEST_F(MetadataStoreTest, UpsertAndGetFile) {
    sync::FileMetadata meta;
    meta.path = "test/file.txt";
    meta.hash = "abc123";
    meta.size = 100;
    meta.mtime = 1234567890;
    meta.created_at = 1234567890;
    meta.updated_at = 1234567890;
    meta.is_deleted = false;
    auto upsert_result = m_Store->upsert_file(meta);
    ASSERT_TRUE(upsert_result.has_value()) << upsert_result.error();
    auto get_result = m_Store->get_file("test/file.txt");
    ASSERT_TRUE(get_result.has_value()) << get_result.error();
    ASSERT_TRUE(get_result.value().has_value());
    EXPECT_EQ(get_result.value()->path, "test/file.txt");
    EXPECT_EQ(get_result.value()->hash, "abc123");
    EXPECT_EQ(get_result.value()->size, 100);
}

TEST_F(MetadataStoreTest, GetAllFiles) {
    sync::FileMetadata f1, f2;
    f1.path = "file1.txt";
    f1.hash = "hash1";
    f1.size = 10;
    f1.mtime = f1.created_at = f1.updated_at = 1000;
    f2.path = "file2.txt";
    f2.hash = "hash2";
    f2.size = 20;
    f2.mtime = f2.created_at = f2.updated_at = 2000;
    auto res = m_Store->upsert_file(f1);
    res = m_Store->upsert_file(f2);
    auto result = m_Store->get_all_files();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2);
}

TEST_F(MetadataStoreTest, GetFilesSince) {
    sync::FileMetadata f1, f2;
    f1.path = "old.txt";
    f1.hash = "hash1";
    f1.size = 10;
    f1.mtime = f1.created_at = f1.updated_at = 1000;
    f2.path = "new.txt";
    f2.hash = "hash2";
    f2.size = 20;
    f2.mtime = f2.created_at = f2.updated_at = 3000;
    auto res = m_Store->upsert_file(f1);
    res = m_Store->upsert_file(f2);
    auto result = m_Store->get_all_files(2000);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value()[0].path, "new.txt");
}

TEST_F(MetadataStoreTest, MarkDeleted) {
    sync::FileMetadata meta;
    meta.path = "to_delete.txt";
    meta.hash = "hash";
    meta.size = 10;
    meta.mtime = meta.created_at = meta.updated_at = 1000;
    meta.is_deleted = false;
    auto res = m_Store->upsert_file(meta);
    auto mark_result = m_Store->mark_deleted("to_delete.txt");
    ASSERT_TRUE(mark_result.has_value());
    auto get_result = m_Store->get_file("to_delete.txt");
    ASSERT_TRUE(get_result.has_value());
    ASSERT_TRUE(get_result.value().has_value());
    EXPECT_TRUE(get_result.value()->is_deleted);
}

TEST_F(MetadataStoreTest, NotesCRUD) {
    sync::NoteMetadata note;
    note.id = "test-uuid";
    note.path = "notes/test-uuid.md";
    note.title = "Test Note";
    note.hash = "notehash";
    note.created_at = note.updated_at = sync::now_ms();
    note.tags = {"tag1", "tag2"};
    auto upsert_result = m_Store->upsert_note(note);
    ASSERT_TRUE(upsert_result.has_value()) << upsert_result.error();
    auto get_result = m_Store->get_note("test-uuid");
    ASSERT_TRUE(get_result.has_value()) << get_result.error();
    ASSERT_TRUE(get_result.value().has_value());
    EXPECT_EQ(get_result.value()->title, "Test Note");
    EXPECT_EQ(get_result.value()->tags.size(), 2);
}

TEST_F(MetadataStoreTest, Tags) {
    sync::NoteMetadata n1, n2;
    n1.id = "note1";
    n1.path = "notes/note1.md";
    n1.title = "Note 1";
    n1.hash = "h1";
    n1.created_at = n1.updated_at = sync::now_ms();
    n1.tags = {"shared", "unique1"};
    n2.id = "note2";
    n2.path = "notes/note2.md";
    n2.title = "Note 2";
    n2.hash = "h2";
    n2.created_at = n2.updated_at = sync::now_ms();
    n2.tags = {"shared", "unique2"};
    auto res = m_Store->upsert_note(n1);
    res = m_Store->upsert_note(n2);
    auto tags_result = m_Store->get_all_tags();
    ASSERT_TRUE(tags_result.has_value());
    // Should have 3 tags: shared (count=2), unique1 (count=1), unique2 (count=1)
    EXPECT_EQ(tags_result.value().size(), 3);
    // Find shared tag
    bool found_shared = false;
    for (const auto& tag : tags_result.value()) {
        if (tag.name == "shared") {
            found_shared = true;
            EXPECT_EQ(tag.count, 2);
        }
    }
    EXPECT_TRUE(found_shared);
}

TEST_F(MetadataStoreTest, AuthTokens) {
    auto now = sync::now_ms() / 1000;
    auto store_result = m_Store->store_token("test-token", now + 3600);
    ASSERT_TRUE(store_result.has_value());
    auto valid_result = m_Store->validate_token("test-token");
    ASSERT_TRUE(valid_result.has_value());
    EXPECT_TRUE(valid_result.value());
    auto invalid_result = m_Store->validate_token("wrong-token");
    ASSERT_TRUE(invalid_result.has_value());
    EXPECT_FALSE(invalid_result.value());
}

TEST_F(MetadataStoreTest, AuthTokenExpiry) {
    auto now = sync::now_ms() / 1000;
    auto res = m_Store->store_token("expired-token", now - 100); // Already expired
    auto result = m_Store->validate_token("expired-token");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST_F(FileServiceTest, PutAndGetFile) {
    std::vector<u8> content = {'H', 'e', 'l', 'l', 'o'};
    auto put_result = m_Service->put_file("test.txt", content);
    ASSERT_TRUE(put_result.has_value()) << put_result.error();
    EXPECT_EQ(put_result.value().size, 5);
    EXPECT_FALSE(put_result.value().hash.empty());
    auto get_result = m_Service->get_file("test.txt");
    ASSERT_TRUE(get_result.has_value()) << get_result.error();
    EXPECT_EQ(get_result.value(), content);
}

TEST_F(FileServiceTest, DeleteFile) {
    std::vector<u8> content = {'T', 'e', 's', 't'};
    auto res = m_Service->put_file("to_delete.txt", content);
    auto delete_result = m_Service->delete_file("to_delete.txt");
    ASSERT_TRUE(delete_result.has_value());
    auto get_result = m_Service->get_file("to_delete.txt");
    EXPECT_FALSE(get_result.has_value()); // Should fail, file is deleted
}

TEST_F(FileServiceTest, ListFiles) {
    auto res = m_Service->put_file("file1.txt", std::vector<u8>{'1'});
    res = m_Service->put_file("file2.txt", std::vector<u8>{'2'});
    auto result = m_Service->list_files();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2);
}

TEST_F(FileServiceTest, GetMetadata) {
    std::vector<u8> content = {'D', 'a', 't', 'a'};
    auto res = m_Service->put_file("meta_test.txt", content);
    auto result = m_Service->get_metadata("meta_test.txt");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value()->path, "meta_test.txt");
    EXPECT_EQ(result.value()->size, 4);
}

TEST(ConfigTest, GetDataDir) {
    auto data_dir = sap::cloud::get_data_dir();
    EXPECT_FALSE(data_dir.empty());
    EXPECT_TRUE(data_dir.string().find("sapcloud") != std::string::npos);
}

TEST(ConfigTest, DefaultConfig) {
    auto result = sap::cloud::load_config_default();
    // May or may not find a config file, but shouldn't crash
    ASSERT_TRUE(result.has_value());
    // Check defaults
    EXPECT_EQ(result.value().server.port, 8080);
    EXPECT_EQ(result.value().server.host, "127.0.0.1");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}