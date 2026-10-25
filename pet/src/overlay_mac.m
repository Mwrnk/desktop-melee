/* SPDX-License-Identifier: GPL-3.0-or-later */
/* macOS window settings for the desktop overlay that SDL has no API for. */
#import <AppKit/AppKit.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include "pet.h"

void pet_overlay_mac_setup(SDL_Window* window) {
    NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (nswindow == nil) {
        SDL_Log("pet: no NSWindow for the overlay");
        return;
    }
    /* Clicks, drags and scrolls go to the windows underneath. */
    nswindow.ignoresMouseEvents = YES;
    nswindow.opaque = NO;
    nswindow.backgroundColor = NSColor.clearColor;
    nswindow.hasShadow = NO;
    /* On every Space, beside fullscreen apps, and out of Cmd-` window cycling. */
    nswindow.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorStationary |
                                  NSWindowCollectionBehaviorFullScreenAuxiliary |
                                  NSWindowCollectionBehaviorIgnoresCycle;
    SDL_Log("pet: overlay window %.0fx%.0f, click-through", nswindow.frame.size.width,
            nswindow.frame.size.height);
    /* The Metal layer must be non-opaque or the compositor ignores our alpha. */
    NSMutableArray<NSView*>* views = [NSMutableArray arrayWithObject:nswindow.contentView];
    while (views.count > 0) {
        NSView* view = views.lastObject;
        [views removeLastObject];
        [views addObjectsFromArray:view.subviews];
        for (CALayer* layer = view.layer; layer != nil; layer = nil) {
            NSMutableArray<CALayer*>* layers = [NSMutableArray arrayWithObject:layer];
            while (layers.count > 0) {
                CALayer* l = layers.lastObject;
                [layers removeLastObject];
                if (l.sublayers != nil) {
                    [layers addObjectsFromArray:l.sublayers];
                }
                if ([l isKindOfClass:NSClassFromString(@"CAMetalLayer")]) {
                    SDL_Log("pet: CAMetalLayer opaque=%d", l.opaque ? 1 : 0);
                }
            }
        }
    }
}

void pet_mac_screen_geometry(SDL_Window* window, float* width_pt, float* height_pt,
                             float* floor_pt) {
    NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    NSScreen* screen = nswindow.screen != nil ? nswindow.screen : NSScreen.mainScreen;
    const NSRect frame = screen.frame;
    const NSRect visible = screen.visibleFrame;
    *width_pt = (float)frame.size.width;
    *height_pt = (float)frame.size.height;
    /* A bottom Dock shrinks the visible frame from below; its top is the floor.
     * (A hidden or side Dock leaves this near 0: the floor is the screen edge.) */
    *floor_pt = (float)(visible.origin.y - frame.origin.y);
}
