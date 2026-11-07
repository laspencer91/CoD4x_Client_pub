#include "source.hpp"
#include <windows.h>
#include <cstdio>

namespace cod4x::rml {
Source::~Source() { if (notification) FindCloseChangeNotification(notification); }
bool Source::Set(const char* directory) {
    const std::string requested = directory ? directory : "";
    if (requested == root) return true;
    HANDLE next = nullptr;
    if (!requested.empty()) {
        const DWORD attributes = GetFileAttributesA(requested.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        next = FindFirstChangeNotificationA(requested.c_str(), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        if (next == INVALID_HANDLE_VALUE) return false;
    }
    if (notification) FindCloseChangeNotification(notification);
    notification = next;
    root = requested;
    next_scan = 0; changed_at = -1;
    return true;
}
bool Source::Read(const std::string& path, std::vector<unsigned char>& bytes) const {
    if (!Enabled() || path.compare(0, 7, "ui/rml/") || path.find("..") != path.npos ||
        path.find(':') != path.npos || path.find('\\') != path.npos) return false;
    const std::string filename = root + "/" + path.substr(7);
    FILE* file = std::fopen(filename.c_str(), "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END); const long length = std::ftell(file); std::rewind(file);
    bool ok = length >= 0 && length <= 16 * 1024 * 1024;
    if (ok) {
        bytes.resize(size_t(length));
        ok = bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    }
    std::fclose(file);
    return ok;
}
bool Source::Changed(double seconds) {
    if (!Enabled() || seconds < next_scan) return false;
    next_scan = seconds + 0.1;
    if (WaitForSingleObject(notification, 0) == WAIT_OBJECT_0) {
        FindNextChangeNotification(notification);
        changed_at = seconds;
        return false;
    }
    if (changed_at >= 0 && seconds - changed_at >= 0.1) { changed_at = -1; return true; }
    return false;
}
}
