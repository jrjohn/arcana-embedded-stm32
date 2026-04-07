/**
 * @file ff_host_stub.cpp
 * @brief Tiny in-memory FatFs implementation for host tests.
 *
 * Backed by a std::vector<FileEntry>. Each entry holds the path + bytes.
 * Open/read/write/seek/lseek/unlink/rename are implemented enough to satisfy
 * RegistrationServiceImpl::saveToFile/loadFromFile and the FatFs paths inside
 * AtsStorageServiceImpl that don't require directory iteration.
 *
 * f_opendir / f_readdir lazily snapshot the file table on opendir() so the
 * iteration order is deterministic across tests.
 */
#include "ff.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

struct FileEntry {
    std::string path;
    std::vector<uint8_t> bytes;
};

std::vector<FileEntry> g_files;

FileEntry* findEntry(const char* path) {
    for (auto& e : g_files) {
        if (e.path == path) return &e;
    }
    return nullptr;
}

struct DirSnapshot {
    std::vector<FileEntry> entries;
    size_t                 index;
};

} // anonymous namespace

extern "C" {

void test_ff_reset(void) {
    g_files.clear();
}

void test_ff_create(const char* path, const uint8_t* data, UINT len) {
    if (FileEntry* e = findEntry(path)) {
        e->bytes.assign(data, data + len);
        return;
    }
    FileEntry e;
    e.path = path;
    e.bytes.assign(data, data + len);
    g_files.push_back(std::move(e));
}

FRESULT test_ff_read(const char* path, uint8_t* buf, UINT bufSize, UINT* out_len) {
    FileEntry* e = findEntry(path);
    if (!e) return FR_NO_FILE;
    UINT n = (UINT)e->bytes.size();
    if (n > bufSize) n = bufSize;
    std::memcpy(buf, e->bytes.data(), n);
    if (out_len) *out_len = n;
    return FR_OK;
}

int test_ff_exists(const char* path) {
    return findEntry(path) ? 1 : 0;
}

/* ── FatFs API ───────────────────────────────────────────────────────────── */

/* Singleton stub FATFS — fs->n_fats=2 picks the truncate-safe production
 * path (matches deployed config since 2026-03-19). */
static FATFS sStubFatFs = { /*n_fats*/ 2, /*dummy*/ 0 };

FRESULT f_open(FIL* fp, const char* path, BYTE mode) {
    if (!fp || !path) return FR_INVALID_PARAMETER;
    std::memset(fp, 0, sizeof(*fp));
    fp->flag    = mode;
    fp->obj.fs  = &sStubFatFs;
    FileEntry* e = findEntry(path);
    if (!e) {
        if (mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS)) {
            FileEntry n;
            n.path = path;
            g_files.push_back(std::move(n));
            e = &g_files.back();
        } else {
            return FR_NO_FILE;
        }
    } else if (mode & FA_CREATE_ALWAYS) {
        e->bytes.clear();
    }
    fp->_entry      = e;
    fp->fptr        = 0;
    fp->obj.objsize = e->bytes.size();
    return FR_OK;
}

FRESULT f_close(FIL* fp) {
    if (!fp) return FR_INVALID_PARAMETER;
    fp->_entry = nullptr;
    return FR_OK;
}

FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br) {
    if (!fp || !fp->_entry) { if (br) *br = 0; return FR_INVALID_OBJECT; }
    auto* e = static_cast<FileEntry*>(fp->_entry);
    UINT remaining = (fp->fptr < e->bytes.size())
                     ? (UINT)(e->bytes.size() - fp->fptr) : 0;
    UINT n = btr < remaining ? btr : remaining;
    if (n > 0) std::memcpy(buff, e->bytes.data() + fp->fptr, n);
    fp->fptr += n;
    if (br) *br = n;
    return FR_OK;
}

FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
    if (!fp || !fp->_entry) { if (bw) *bw = 0; return FR_INVALID_OBJECT; }
    auto* e = static_cast<FileEntry*>(fp->_entry);
    if (fp->fptr + btw > e->bytes.size()) {
        e->bytes.resize(fp->fptr + btw, 0xFF);
    }
    std::memcpy(e->bytes.data() + fp->fptr,
                static_cast<const uint8_t*>(buff), btw);
    fp->fptr        += btw;
    fp->obj.objsize  = e->bytes.size();
    if (bw) *bw = btw;
    return FR_OK;
}

FRESULT f_lseek(FIL* fp, FSIZE_t ofs) {
    if (!fp || !fp->_entry) return FR_INVALID_OBJECT;
    auto* e = static_cast<FileEntry*>(fp->_entry);
    if (ofs > e->bytes.size()) {
        e->bytes.resize(ofs, 0xFF);
        fp->obj.objsize = e->bytes.size();
    }
    fp->fptr = ofs;
    return FR_OK;
}

FRESULT f_sync(FIL* /*fp*/) { return FR_OK; }

FRESULT f_truncate(FIL* fp) {
    if (!fp || !fp->_entry) return FR_INVALID_OBJECT;
    auto* e = static_cast<FileEntry*>(fp->_entry);
    if (fp->fptr < e->bytes.size()) {
        e->bytes.resize(fp->fptr);
        fp->obj.objsize = e->bytes.size();
    }
    return FR_OK;
}

FRESULT f_expand(FIL* fp, FSIZE_t fsz, BYTE /*opt*/) {
    if (!fp || !fp->_entry) return FR_INVALID_OBJECT;
    auto* e = static_cast<FileEntry*>(fp->_entry);
    if (fsz > e->bytes.size()) {
        e->bytes.resize(fsz, 0xFF);
        fp->obj.objsize = e->bytes.size();
    }
    return FR_OK;
}

FRESULT f_unlink(const char* path) {
    for (auto it = g_files.begin(); it != g_files.end(); ++it) {
        if (it->path == path) { g_files.erase(it); return FR_OK; }
    }
    return FR_NO_FILE;
}

FRESULT f_rename(const char* old_path, const char* new_path) {
    FileEntry* src = findEntry(old_path);
    if (!src) return FR_NO_FILE;
    /* If target already exists, remove it first */
    for (auto it = g_files.begin(); it != g_files.end(); ++it) {
        if (it->path == new_path) { g_files.erase(it); break; }
        // src may have been invalidated by erase — re-find below
    }
    src = findEntry(old_path);
    if (!src) return FR_NO_FILE;
    src->path = new_path;
    return FR_OK;
}

FRESULT f_opendir(DIR* dp, const char* /*path*/) {
    if (!dp) return FR_INVALID_PARAMETER;
    auto* snap = new DirSnapshot{};
    snap->entries = g_files;
    snap->index   = 0;
    dp->_dir = snap;
    return FR_OK;
}

FRESULT f_closedir(DIR* dp) {
    if (!dp || !dp->_dir) return FR_INVALID_OBJECT;
    delete static_cast<DirSnapshot*>(dp->_dir);
    dp->_dir = nullptr;
    return FR_OK;
}

FRESULT f_readdir(DIR* dp, FILINFO* fno) {
    if (!dp || !dp->_dir || !fno) return FR_INVALID_PARAMETER;
    auto* snap = static_cast<DirSnapshot*>(dp->_dir);
    if (snap->index >= snap->entries.size()) {
        fno->fname[0] = '\0';
        return FR_OK;
    }
    const FileEntry& e = snap->entries[snap->index++];
    std::strncpy(fno->fname, e.path.c_str(), sizeof(fno->fname) - 1);
    fno->fname[sizeof(fno->fname) - 1] = '\0';
    fno->fsize    = e.bytes.size();
    fno->fattrib  = 0;
    return FR_OK;
}

FRESULT f_mount(FATFS* /*fs*/, const char* /*path*/, BYTE /*opt*/) {
    return FR_OK;
}

FSIZE_t f_size(FIL* fp) {
    if (!fp || !fp->_entry) return 0;
    return static_cast<FileEntry*>(fp->_entry)->bytes.size();
}

DWORD f_tell(FIL* fp) {
    return fp ? fp->fptr : 0;
}

} // extern "C"
