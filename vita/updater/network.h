#pragma once
#include "core.h"
namespace Update {
bool InitNetwork(std::string &error);
void StopNetwork();
bool FetchManifest(std::string &text, std::string &error);
// Startup probe: no custom UI, cancellation buttons or connection prompt.
// Only uses an already connected network and bounds the HTTPS request.
bool FetchStartupManifest(std::string &text, std::string &error);
bool Download(const Manifest &manifest, const std::string &metadata, std::string &error);
bool VerifyDownload(const Manifest &manifest, std::string &error);
constexpr const char *DownloadPath="ux0:data/reLCS-update/release.vpk.part";
}
