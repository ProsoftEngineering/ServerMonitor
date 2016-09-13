//
//  Created by Kevin Wojniak on 9/12/16.
//  Copyright 2016 Kevin Wojniak. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef void (^MonitorCompletionHandler)(NSString *error);

@protocol Monitor <NSObject>

@required

@property (readwrite, copy) MonitorCompletionHandler completionHandler;

- (void)run;

@end

@interface WebsiteMonitor : NSObject <Monitor>

@property (readwrite, copy) NSURL *url;

@end

@interface ServiceMonitor : NSObject <Monitor, NSStreamDelegate>

@property (readwrite, copy) NSString *host;
@property (readwrite) UInt32 port;

@end

@implementation WebsiteMonitor

@synthesize completionHandler;

- (void)run
{
    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
    NSURLSessionDataTask *task = [session dataTaskWithURL:self.url completionHandler:^(NSData * _Nullable data, NSURLResponse * _Nullable response, NSError * _Nullable error) {
        if (error) {
            self.completionHandler(error.localizedDescription);
        } else {
            NSHTTPURLResponse *httpresponse = (NSHTTPURLResponse*)response;
            const NSInteger code = httpresponse.statusCode;
            if (code != 200) {
                self.completionHandler([NSString stringWithFormat:@"HTTP Status %ld", code]);
            } else {
                self.completionHandler(nil);
            }
        }
    }];
    [task resume];
}

@end

@implementation ServiceMonitor

@synthesize completionHandler;

- (void)run
{
    CFReadStreamRef readStream = NULL;
    CFStreamCreatePairWithSocketToHost(kCFAllocatorDefault, (__bridge CFStringRef)self.host, self.port, &readStream, NULL);
    NSInputStream *inputStream = (__bridge NSInputStream*)readStream;
    inputStream.delegate = self;
    [inputStream scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
    [inputStream open];
}

- (void)stream:(NSStream *)aStream handleEvent:(NSStreamEvent)eventCode
{
    if (eventCode == NSStreamEventOpenCompleted) {
        [aStream close];
        self.completionHandler(nil);
    } else if (eventCode == NSStreamEventErrorOccurred) {
        [aStream close];
        self.completionHandler(aStream.streamError.localizedDescription);
    }
}

@end

@interface ServerMonitor : NSObject

@property (readwrite, copy) NSDictionary *config;
@property (readwrite, strong) NSMutableArray<id<Monitor>> *monitors;

@end

@implementation ServerMonitor

- (void)run
{
    self.monitors = [NSMutableArray array];

    NSArray *servers = [self.config objectForKey:@"servers"];
    for (NSDictionary *item in servers) {
        NSString *url = [item objectForKey:@"url"];
        if (url) {
            WebsiteMonitor *mon = [[WebsiteMonitor alloc] init];
            mon.url = [NSURL URLWithString:url];
            mon.completionHandler = ^(NSString *error) {
                printf("%s ERROR: %s\n", url.UTF8String, error.UTF8String);
            };
            [self.monitors addObject:mon];
            continue;
        }
        
        NSString *host = [item objectForKey:@"host"];
        NSNumber *port = [item objectForKey:@"port"];
        ServiceMonitor *mon = [[ServiceMonitor alloc] init];
        mon.host = host;
        mon.port = port.unsignedIntValue;
        mon.completionHandler = ^(NSString *error) {
            printf("%s ERROR: %s\n", host.UTF8String, error.UTF8String);
        };
        [self.monitors addObject:mon];
    }
    
    [self.monitors enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(id  _Nonnull obj, NSUInteger idx, BOOL * _Nonnull stop) {
        [obj run];
    }];
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        if (argc != 2) {
            printf("Missing config arg\n");
            return EXIT_FAILURE;
        }
        NSString *file = [NSString stringWithUTF8String:argv[1]];
        NSError *err = nil;
        NSData *data = [NSData dataWithContentsOfFile:file options:0 error:&err];
        if (err) {
            printf(" Invalid file: %s\n", err.localizedDescription.UTF8String);
            return EXIT_FAILURE;
        }
        id plist = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
        if (err) {
            printf("Invalid config: %s\n", err.localizedDescription.UTF8String);
            return EXIT_FAILURE;
        }
        if (!plist || ![plist isKindOfClass:[NSDictionary class]]) {
            printf("Invalid config\n");
            return EXIT_FAILURE;
        }
        ServerMonitor *mon = [[ServerMonitor alloc] init];
        mon.config = plist;
        [mon run];
        [[NSRunLoop currentRunLoop] run];
    }
    return EXIT_SUCCESS;
}
