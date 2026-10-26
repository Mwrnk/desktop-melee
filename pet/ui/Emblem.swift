// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in Smash Bros. emblem for the menu bar until the game's own texture
// is decoded (menu bar design, step 3): a disc cut by a vertical and a
// horizontal line.
import AppKit

enum SmashEmblem {
    static func image(size: CGFloat) -> NSImage {
        let image = NSImage(size: NSSize(width: size, height: size), flipped: false) { rect in
            let inset = rect.insetBy(dx: rect.width * 0.06, dy: rect.height * 0.06)
            let disc = NSBezierPath(ovalIn: inset)
            NSColor.black.setFill()
            disc.fill()
            // Cut the two lines out of the disc.
            NSGraphicsContext.current?.compositingOperation = .clear
            let gap = rect.width * 0.09
            NSRect(x: rect.minX + rect.width * 0.38, y: rect.minY, width: gap, height: rect.height).fill()
            NSRect(x: rect.minX, y: rect.minY + rect.height * 0.32, width: rect.width, height: gap).fill()
            return true
        }
        image.isTemplate = true
        return image
    }
}
