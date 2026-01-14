#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sap_cloud/storage/metadata.h>
#include <sap_fs/fs.h>
#include <sap_sync/sync_types.h>

using namespace sap;
using namespace sap::drive;

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

TEST_F(FilesystemTest, WriteAndRead) {
    std::vector<u8> content = {'H', 'e', 'l', 'l', 'o'};
    auto writeResult = m_Fs->write("test.txt", content);
    ASSERT_TRUE(writeResult.has_value()) << writeResult.error();
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
    auto writeResult = m_Fs->write("text.txt", "Hello, World!");
    ASSERT_TRUE(writeResult.has_value());
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
    auto upsertResult = m_Store->upsert_file(meta);
    ASSERT_TRUE(upsertResult.has_value()) << upsertResult.error();
    auto getResult = m_Store->get_file("test/file.txt");
    ASSERT_TRUE(getResult.has_value()) << getResult.error();
    ASSERT_TRUE(getResult.value().has_value());
    EXPECT_EQ(getResult.value()->path, "test/file.txt");
    EXPECT_EQ(getResult.value()->hash, "abc123");
    EXPECT_EQ(getResult.value()->size, 100);
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
    auto markResult = m_Store->mark_deleted("to_delete.txt");
    ASSERT_TRUE(markResult.has_value());
    auto getResult = m_Store->get_file("to_delete.txt");
    ASSERT_TRUE(getResult.has_value());
    ASSERT_TRUE(getResult.value().has_value());
    EXPECT_TRUE(getResult.value()->is_deleted);
}

TEST_F(MetadataStoreTest, NotesCRUD) {
    sync::NoteMetadata note;
    note.id = "test-uuid";
    note.path = "notes/test-uuid.md";
    note.title = "Test Note";
    note.hash = "notehash";
    note.created_at = note.updated_at = sync::now_ms();
    note.tags = {"tag1", "tag2"};
    auto upsertResult = m_Store->upsert_note(note);
    ASSERT_TRUE(upsertResult.has_value()) << upsertResult.error();
    auto getResult = m_Store->get_note("test-uuid");
    ASSERT_TRUE(getResult.has_value()) << getResult.error();
    ASSERT_TRUE(getResult.value().has_value());
    EXPECT_EQ(getResult.value()->title, "Test Note");
    EXPECT_EQ(getResult.value()->tags.size(), 2);
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
    auto tagsResult = m_Store->get_all_tags();
    ASSERT_TRUE(tagsResult.has_value());
    // Should have 3 tags: shared (count=2), unique1 (count=1), unique2 (count=1)
    EXPECT_EQ(tagsResult.value().size(), 3);
    // Find shared tag
    bool foundShared = false;
    for (const auto& tag : tagsResult.value()) {
        if (tag.name == "shared") {
            foundShared = true;
            EXPECT_EQ(tag.count, 2);
        }
    }
    EXPECT_TRUE(foundShared);
}

TEST_F(MetadataStoreTest, AuthTokens) {
    auto now = sync::now_ms() / 1000;
    auto storeResult = m_Store->store_token("test-token", now + 3600);
    ASSERT_TRUE(storeResult.has_value());
    auto validResult = m_Store->validate_token("test-token");
    ASSERT_TRUE(validResult.has_value());
    EXPECT_TRUE(validResult.value());
    auto invalidResult = m_Store->validate_token("wrong-token");
    ASSERT_TRUE(invalidResult.has_value());
    EXPECT_FALSE(invalidResult.value());
}

TEST_F(MetadataStoreTest, AuthTokenExpiry) {
    auto now = sync::now_ms() / 1000;
    auto res = m_Store->store_token("expired-token", now - 100); // Already expired
    auto result = m_Store->validate_token("expired-token");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}