#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Update {
constexpr const char *Repository = "https://github.com/fauxrougee/GTALCS-psvita-port/";
constexpr const char *MetadataURL = "https://github.com/fauxrougee/GTALCS-psvita-port/releases/latest/download/update.txt";
struct Manifest {
    std::string version, url, sha256;
    uint64_t size=0, unpacked=0;
};
int Version(const std::string &text);
bool ParseManifest(const std::string &text, Manifest &out, std::string &error);
bool LaunchUpdate(const char *text);
bool SafePath(const std::string &path);
bool ValidateSfo(const std::vector<unsigned char> &data, const std::string &version);
std::string SfoVersion(const std::vector<unsigned char> &data);
struct Entry {
    std::string name;
    uint32_t crc, packed, unpacked, offset;
    uint16_t method, flags;
};
bool ReadArchive(const char *path, const Manifest &manifest, std::vector<Entry> &entries,
                 std::string &error);
using MakeDirectory = bool (*)(const char *);
using Progress = bool (*)(uint64_t, uint64_t);
bool ExtractArchive(const char *path, const char *destination, const Manifest &manifest,
                    MakeDirectory mkdir, Progress progress, std::string &error);
}
