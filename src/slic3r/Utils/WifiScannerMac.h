///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef WiFiScanner_h
#define WiFiScanner_h

#import <Foundation/Foundation.h>
extern "C" {
#import <CoreWLAN/CoreWLAN.h>
}

@interface WifiScannerMac : NSObject

- (instancetype)init;
- (void)dealloc;
- (NSArray<NSString *> *)scan_ssids;
- (NSString *)retrieve_password_for_ssid:(NSString *)ssid;
- (NSString *)current_ssid;

@property (strong, nonatomic) CWInterface *wifiInterface;

@end

#endif /* WiFiScanner_h */
