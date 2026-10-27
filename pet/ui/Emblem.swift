// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in menu bar icon until the game's own emblem is decoded from the
// player's disc: a small stage (three floating platforms over a main floor),
// drawn here so the repository holds no game artwork.
import AppKit

enum SmashEmblem {
    static func image(size: CGFloat) -> NSImage {
        let image = NSImage(size: NSSize(width: size, height: size), flipped: true) { rect in
            let w = rect.width, h = rect.height
            let bar = h * 0.11
            NSColor.black.setFill()
            // Floating platforms: one on top, two below it.
            for (x, y) in [(0.36, 0.18), (0.10, 0.40), (0.62, 0.40)] {
                NSBezierPath(roundedRect: NSRect(x: w * x, y: h * y, width: w * 0.28, height: bar),
                             xRadius: bar / 2, yRadius: bar / 2).fill()
            }
            // The main floor and the stage below it.
            let floor = NSBezierPath()
            floor.move(to: NSPoint(x: w * 0.04, y: h * 0.62))
            floor.line(to: NSPoint(x: w * 0.96, y: h * 0.62))
            floor.line(to: NSPoint(x: w * 0.96, y: h * 0.62 + bar))
            floor.line(to: NSPoint(x: w * 0.82, y: h * 0.62 + bar))
            floor.line(to: NSPoint(x: w * 0.64, y: h * 0.90))
            floor.line(to: NSPoint(x: w * 0.36, y: h * 0.90))
            floor.line(to: NSPoint(x: w * 0.18, y: h * 0.62 + bar))
            floor.line(to: NSPoint(x: w * 0.04, y: h * 0.62 + bar))
            floor.close()
            floor.fill()
            return true
        }
        image.isTemplate = true
        return image
    }
}
