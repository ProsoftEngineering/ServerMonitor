#include <Foundation/Foundation.h>
#include <string>

bool UrlDownload(const std::string& url, double timeout, std::string& errorMessage) {
    NSURL *nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:nsurl cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:timeout];
    request.HTTPMethod = @"HEAD";
    __block bool success = false;
    __block bool done = false;
    void (^handler)(NSData *, NSURLResponse *, NSError *) = ^(NSData *data __unused, NSURLResponse *response, NSError *error) {
        if (error) {
            errorMessage.assign(error.localizedDescription.UTF8String);
        } else {
            NSHTTPURLResponse *httpresponse = (NSHTTPURLResponse*)response;
            const NSInteger code = httpresponse.statusCode;
            if (code != 200) {
                errorMessage.assign([NSString stringWithFormat:@"HTTP Status %ld", code].UTF8String);
            } else {
                success = true;
            }
        }
        done = true;
    };
    [[session dataTaskWithRequest:request completionHandler:handler] resume];
    while (!done) {
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    return success;
}
