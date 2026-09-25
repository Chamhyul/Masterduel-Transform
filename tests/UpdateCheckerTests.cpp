#include "../AutoTransform/AutoTransform_Update.h"

#include <cassert>
#include <chrono>
#include <thread>

bool AT_FetchLatestReleaseJSON(std::string& response) {
    response = R"({"tag_name":"v0.1.4"})";
    return true;
}

void AT_OpenLatestReleasePage() {}

int main() {
    assert(!AT_IsNewerReleaseTag(R"({"tag_name":"v0.1.1"})", 0, 1, 3));
    assert(!AT_IsNewerReleaseTag(R"({"tag_name":"0.1.2"})", 0, 1, 3));
    assert(!AT_IsNewerReleaseTag(R"({"tag_name":"v0.1.3"})", 0, 1, 3));
    assert(AT_IsNewerReleaseTag(R"({"tag_name":"v0.1.3"})", 0, 1, 2));
    assert(AT_IsNewerReleaseTag(R"({"tag_name":"v0.1.4"})", 0, 1, 3));
    assert(AT_IsNewerReleaseTag(R"({"tag_name":"1.0.0"})", 0, 1, 3));
    assert(!AT_IsNewerReleaseTag(R"({"tag_name":"v0.1.4-beta"})", 0, 1, 3));
    assert(!AT_IsNewerReleaseTag(R"({"message":"Not Found"})", 0, 1, 3));

    AT_StartUpdateCheck(0, 1, 3);
    for (int i = 0; i < 100 && !AT_IsUpdateAvailable(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(AT_IsUpdateAvailable());
}
