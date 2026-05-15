///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/ Copyright (c) Prusa Research 2019 David Kocík @kocikdav
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_GUI_RemovableDriveManagerMM_h_
#define slic3r_GUI_RemovableDriveManagerMM_h_

#import <Cocoa/Cocoa.h>

@interface RemovableDriveManagerMM : NSObject

-(instancetype) init;
-(void) add_unmount_observer;
-(void) on_device_unmount: (NSNotification*) notification;
-(NSArray*) list_dev;
-(void)eject_drive:(NSString *)path;
@end

#endif // slic3r_GUI_RemovableDriveManagerMM_h_
