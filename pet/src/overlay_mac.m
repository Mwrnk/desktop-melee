/* SPDX-License-Identifier: GPL-3.0-or-later */
/* macOS window settings for the desktop overlay that SDL has no API for. */
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <ImageIO/ImageIO.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include "pet.h"

/* The overlay window, and the screen it is on (the menu's choice). */
static NSWindow* s_overlay;

static NSScreen* overlay_screen(void) {
    return s_overlay.screen != nil ? s_overlay.screen : NSScreen.mainScreen;
}

void pet_overlay_mac_setup(SDL_Window* window) {
    NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (nswindow == nil) {
        SDL_Log("pet: no NSWindow for the overlay");
        return;
    }
    s_overlay = nswindow;
    /* Clicks, drags and scrolls go to the windows underneath. */
    nswindow.ignoresMouseEvents = YES;
    nswindow.opaque = NO;
    nswindow.backgroundColor = NSColor.clearColor;
    nswindow.hasShadow = NO;
    /* Just below the menu bar (and just above the notch island). The menu bar
     * is transparent, so the island and the HUD in it show through it, while
     * the menu bar's own items stay on top, visible and clickable. */
    nswindow.level = NSMainMenuWindowLevel - 1;
    /* On every Space, beside fullscreen apps, and out of Cmd-` window cycling. */
    nswindow.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorStationary |
                                  NSWindowCollectionBehaviorFullScreenAuxiliary |
                                  NSWindowCollectionBehaviorIgnoresCycle;
    SDL_Log("pet: overlay window %.0fx%.0f, click-through", nswindow.frame.size.width,
            nswindow.frame.size.height);
    /* The menu bar app (pet/ui, Swift). */
    pet_ui_install((__bridge void*)nswindow);
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

/* The Dock bar's frame from the Accessibility API (exact; needs the user to
 * grant Accessibility once). Returns false when not permitted. Output is in
 * points with y up from the bottom of the primary screen. */
static bool dock_frame_ax(NSScreen* screen, float* left, float* right, float* top,
                          bool* on_screen) {
    /* MELEE_PET_AX_PROMPT=0: never show the permission dialog (test runs). */
    const char* prompt_env = getenv("MELEE_PET_AX_PROMPT");
    const bool prompt = !(prompt_env != NULL && prompt_env[0] == '0');
    NSDictionary* opts = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt : @(prompt)};
    if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts)) {
        return false;
    }
    NSRunningApplication* dock =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"]
            .firstObject;
    if (dock == nil) {
        return false;
    }
    AXUIElementRef app = AXUIElementCreateApplication(dock.processIdentifier);
    CFArrayRef children = NULL;
    bool found = false;
    if (AXUIElementCopyAttributeValue(app, kAXChildrenAttribute, (CFTypeRef*)&children) ==
            kAXErrorSuccess &&
        children != NULL) {
        for (CFIndex i = 0; i < CFArrayGetCount(children) && !found; i++) {
            AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
            CFTypeRef role = NULL, pos = NULL, size = NULL;
            AXUIElementCopyAttributeValue(child, kAXRoleAttribute, &role);
            if (role != NULL && CFEqual(role, kAXListRole) &&
                AXUIElementCopyAttributeValue(child, kAXPositionAttribute, &pos) ==
                    kAXErrorSuccess &&
                AXUIElementCopyAttributeValue(child, kAXSizeAttribute, &size) ==
                    kAXErrorSuccess) {
                CGPoint p;
                CGSize s;
                if (AXValueGetValue(pos, kAXValueCGPointType, &p) &&
                    AXValueGetValue(size, kAXValueCGSizeType, &s) && s.width > 0) {
                    /* AX: top-left origin at the primary screen's top-left.
                     * To Cocoa (y up from the primary's bottom), then to the
                     * overlay screen's own frame. */
                    const NSRect frame = screen.frame;
                    const double primary_h = NSScreen.screens.firstObject.frame.size.height;
                    const double top_y = primary_h - p.y;
                    const double mid_x = p.x + 0.5 * s.width;
                    *left = (float)(p.x - frame.origin.x);
                    *right = (float)(p.x + s.width - frame.origin.x);
                    *top = (float)(top_y - frame.origin.y);
                    *on_screen = mid_x >= NSMinX(frame) && mid_x <= NSMaxX(frame) &&
                                 top_y > NSMinY(frame) && top_y <= NSMaxY(frame);
                    found = true;
                }
            }
            if (role) CFRelease(role);
            if (pos) CFRelease(pos);
            if (size) CFRelease(size);
        }
        CFRelease(children);
    }
    CFRelease(app);
    return found;
}

/* Without Accessibility: estimate the Dock width from its settings (icon size
 * and how many icons it shows), centred on the screen. */
static void dock_frame_estimate(float screen_w, float* left, float* right) {
    NSUserDefaults* dock = [[NSUserDefaults alloc] initWithSuiteName:@"com.apple.dock"];
    double tile = [dock doubleForKey:@"tilesize"];
    if (tile <= 0) {
        tile = 48;
    }
    NSArray* apps = [dock arrayForKey:@"persistent-apps"];
    NSMutableSet* pinned = [NSMutableSet set];
    for (NSDictionary* app in apps) {
        NSString* bundle = app[@"tile-data"][@"bundle-identifier"];
        if (bundle != nil) {
            [pinned addObject:bundle];
        }
    }
    NSUInteger tiles = 2 + apps.count + [dock arrayForKey:@"persistent-others"].count; /* Finder, Trash */
    for (NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications) {
        if (app.activationPolicy == NSApplicationActivationPolicyRegular &&
            ![app.bundleIdentifier isEqualToString:@"com.apple.finder"] &&
            ![pinned containsObject:app.bundleIdentifier ?: @""]) {
            tiles++;
        }
    }
    id recents = [dock objectForKey:@"show-recents"];
    if (recents == nil || [recents boolValue]) {
        tiles += [dock arrayForKey:@"recent-apps"].count;
    }
    /* Measured: a 62 pt Dock with 16 icons (the game's own included) is
     * ~1100 pt wide, icon pitch, separator and end padding included. */
    const float width = (float)(tiles * tile * 1.11);
    *left = 0.5f * (screen_w - width);
    *right = 0.5f * (screen_w + width);
}

bool pet_mac_dock_frame(float screen_w, float screen_h, float* left, float* right,
                        float* top, bool* exact) {
    (void)screen_h;
    NSScreen* screen = overlay_screen();
    bool on_screen = false;
    *exact = dock_frame_ax(screen, left, right, top, &on_screen);
    if (*exact) {
        return on_screen;
    }
    /* Estimated: a bottom Dock shows as an inset at the bottom of its
     * screen; a hidden Dock leaves none, and then it lives on the primary. */
    const bool inset = NSMinY(screen.visibleFrame) - NSMinY(screen.frame) > 1.0;
    const bool any_inset = ^{
        for (NSScreen* other in NSScreen.screens) {
            if (NSMinY(other.visibleFrame) - NSMinY(other.frame) > 1.0) {
                return true;
            }
        }
        return false;
    }();
    if (!inset && (any_inset || screen != NSScreen.screens.firstObject)) {
        return false;
    }
    dock_frame_estimate(screen_w, left, right);
    return true;
}

void pet_mac_notch(float* left, float* right, float* height) {
    NSScreen* screen = overlay_screen();
    const NSRect frame = screen.frame;
    if (@available(macOS 12.0, *)) {
        const NSRect l = screen.auxiliaryTopLeftArea;
        const NSRect r = screen.auxiliaryTopRightArea;
        if (screen.safeAreaInsets.top > 0 && !NSIsEmptyRect(l) && !NSIsEmptyRect(r)) {
            *left = (float)(NSMaxX(l) - frame.origin.x);
            *right = (float)(NSMinX(r) - frame.origin.x);
            *height = (float)screen.safeAreaInsets.top;
            return;
        }
    }
    /* No notch: an island in the middle of the menu bar. */
    *left = *right = (float)(0.5 * frame.size.width);
    *height = (float)(NSMaxY(frame) - NSMaxY(screen.visibleFrame));
    if (*height < 20.0f) {
        *height = 24.0f;
    }
}

static NSWindow* s_island;
static void island_create(NSRect rect, float height);

void pet_mac_island_place(float left, float right, float height) {
    const NSRect frame = overlay_screen().frame;
    const NSRect rect =
        NSMakeRect(frame.origin.x + left, NSMaxY(frame) - height, right - left, height);
    if (s_island != nil) {
        [s_island setFrame:rect display:YES];
        s_island.contentView.layer.cornerRadius = height * 0.45;
        return;
    }
    island_create(rect, height);
}

static void island_create(NSRect rect, float height) {
    s_island = [[NSWindow alloc] initWithContentRect:rect
                                           styleMask:NSWindowStyleMaskBorderless
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    s_island.opaque = NO;
    s_island.backgroundColor = NSColor.clearColor;
    s_island.hasShadow = NO;
    s_island.ignoresMouseEvents = YES;
    s_island.releasedWhenClosed = NO;
    /* Below the menu bar (whose items draw over it) and below the game
     * overlay, which draws the stock icons and percents on top of it. */
    s_island.level = NSMainMenuWindowLevel - 2;
    s_island.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorStationary |
                                  NSWindowCollectionBehaviorFullScreenAuxiliary |
                                  NSWindowCollectionBehaviorIgnoresCycle;
    NSView* view = s_island.contentView;
    view.wantsLayer = YES;
    /* A wider notch: square top flush with the screen edge, round bottom. */
    view.layer.backgroundColor = NSColor.blackColor.CGColor;
    view.layer.cornerRadius = height * 0.45;
    view.layer.maskedCorners = kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner;
    s_island.alphaValue = 0.0;

    [NSTimer scheduledTimerWithTimeInterval:0.1
                                    repeats:YES
                                      block:^(NSTimer* timer) {
                                          (void)timer;
                                          const bool want = pet_island_wanted();
                                          const bool shown = s_island.alphaValue > 0.0;
                                          if (want == shown && s_island.visible == want) {
                                              return;
                                          }
                                          if (want) {
                                              [s_island orderFrontRegardless];
                                          }
                                          [NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
                                              ctx.duration = 0.25;
                                              s_island.animator.alphaValue = want ? 1.0 : 0.0;
                                          }
                                                              completionHandler:^{
                                                                  if (!want) {
                                                                      [s_island orderOut:nil];
                                                                  }
                                                              }];
                                      }];
}

/* Writes straight RGBA8 pixels as a PNG (asset dumps). */
void pet_mac_write_png(const char* path, const uint8_t* rgba, int width, int height) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate((void*)rgba, (size_t)width, (size_t)height, 8,
                                             (size_t)width * 4, space,
                                             kCGImageAlphaLast | kCGBitmapByteOrderDefault);
    CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
    if (image == NULL) {
        /* CGBitmapContext refuses straight alpha; build the image directly. */
        CGDataProviderRef provider =
            CGDataProviderCreateWithData(NULL, rgba, (size_t)width * height * 4, NULL);
        image = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)width * 4, space,
                              kCGImageAlphaLast | kCGBitmapByteOrderDefault, provider, NULL,
                              false, kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
    }
    if (image != NULL) {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        CGImageDestinationRef dest =
            CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
        if (dest != NULL) {
            CGImageDestinationAddImage(dest, image, NULL);
            CGImageDestinationFinalize(dest);
            CFRelease(dest);
        }
        CGImageRelease(image);
    }
    if (ctx != NULL) {
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(space);
}
