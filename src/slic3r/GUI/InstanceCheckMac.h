///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/ Copyright (c) Prusa Research 2020 - 2021 David Kocík @kocikdav
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_GUI_InstanceCheckMac_h_
#define slic3r_GUI_InstanceCheckMac_h_

#import <Cocoa/Cocoa.h>

@interface OtherInstanceMessageHandlerMac : NSObject

-(instancetype) init;
-(void) add_observer:(NSString *)version;
-(void) message_update:(NSNotification *)note;
-(void) closing_update:(NSNotification *)note;
-(void) bring_forward;
@end

#endif // slic3r_GUI_InstanceCheckMac_h_
