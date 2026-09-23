#pragma once

#include <string>

// A single, process-wide check. Network work never runs in a Premiere callback.
void AT_StartUpdateCheck(int major, int minor, int patch);
bool AT_IsUpdateAvailable();
void AT_OpenLatestReleasePage();

// Implemented by the platform-specific networking source.
bool AT_FetchLatestReleaseJSON(std::string& response);

// Kept separate from the network request so version handling can be verified.
bool AT_IsNewerReleaseTag(const std::string& json, int major, int minor, int patch);
