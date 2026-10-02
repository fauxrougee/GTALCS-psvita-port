#pragma once
#include <cstdint>
#include <string>
namespace Update {
constexpr const char *Work="ux0:data/reLCS-update";
constexpr const char *Package="ux0:data/reLCS-update/package";
bool Directory(const char *path);
bool ClearPackage();
int Promote(const char *path);
bool OpenScreen();
void Screen(const std::string &message, const std::string &detail="", int percent=-1,
            const std::string &buttons="O  Back");
void CloseScreen();
unsigned Buttons();
void Log(const char *format,...);
int StartUpdater();
}
