#include "AutoTransform_Update.h"

#import <Cocoa/Cocoa.h>

bool AT_FetchLatestReleaseJSON(std::string& response) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:@"https://api.github.com/repos/Chamhyul/Masterduel-Transform/releases/latest"];
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url
            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:3.0];
        [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
        [request setValue:@"MasterDuel Transform Update Check/0.1.2" forHTTPHeaderField:@"User-Agent"];

        NSURLResponse* rawResponse = nil;
        NSError* error = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSData* data = [NSURLConnection sendSynchronousRequest:request
                                          returningResponse:&rawResponse error:&error];
#pragma clang diagnostic pop
        if (error || !data || data.length == 0 || data.length > 65536 ||
            ![rawResponse isKindOfClass:[NSHTTPURLResponse class]] ||
            [(NSHTTPURLResponse*)rawResponse statusCode] != 200) return false;
        response.assign(static_cast<const char*>(data.bytes), data.length);
        return true;
    }
}

void AT_OpenLatestReleasePage() {
    @autoreleasepool {
        [[NSWorkspace sharedWorkspace] openURL:
            [NSURL URLWithString:@"https://github.com/Chamhyul/Masterduel-Transform/releases/latest"]];
    }
}
