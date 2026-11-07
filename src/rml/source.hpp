#pragma once
#include <string>
#include <vector>

namespace cod4x::rml {
// Development source overrides are explicit and never fall through to packaged assets.
class Source {
public:
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    ~Source();
    bool Set(const char* directory);
    bool Enabled() const { return !root.empty(); }
    bool Read(const std::string& path, std::vector<unsigned char>& bytes) const;
    bool Changed(double seconds);
private:
    std::string root;
    void* notification = nullptr;
    double next_scan = 0, changed_at = -1;
};
}
