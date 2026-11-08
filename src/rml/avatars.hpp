#pragma once
#include <RmlUi/Core/Types.h>
#include <vector>

namespace cod4x::rml::avatars {
// Player images from a local folder (the client's app data, avatars\): <key>.png or <key>.jpg, where the key is the
// player's name without colour codes, in lower case, with every character but a-z, 0-9 and _ replaced by _. The HUD
// knows only the folder and this naming rule; whoever fills the folder (a tournament admin by hand, a launcher from
// its service) is outside the game.
//
// A worker thread looks for, reads and decodes the images (Windows Imaging Component), crops each to its centred
// square and scales it to at most 128 x 128, so the game's main thread never touches the disk or a decoder.
void SetDirectory(const char* utf8_directory);
void Shutdown();
Rml::String Key(const Rml::String& name);
// "avatars/<key>" for an <img> in a ui/rml document once the name's image is decoded, else empty. A name without an
// image is looked up again every few seconds, so a file added during a match appears.
Rml::String Source(const Rml::String& name, double now);
// "avatars/ghost/<key>" for a Source() path: the same image desaturated, faint, and faded in at the left and out
// toward the right, to sit behind text. Empty for an empty source.
Rml::String Ghost(const Rml::String& source);
bool IsAvatarPath(const Rml::String& path);
// The decoded image for ui/rml/avatars/<key> or ui/rml/avatars/ghost/<key>, as an uncompressed 32-bit TGA for the
// texture loader.
bool Read(const Rml::String& path, std::vector<unsigned char>& bytes);
}
