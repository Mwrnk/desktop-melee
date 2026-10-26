// SPDX-License-Identifier: GPL-3.0-or-later
// Melee's own assets for the popover, decoded from the user's disc at boot
// (pet/src/assets.c): the text font, the character roster's face icons and
// the per-costume portraits.
import AppKit
import PetBridge
import SwiftUI

// MARK: - Images

enum GameImages {
    /// A CGImage from straight (non-premultiplied) RGBA8 pixels.
    static func cgImage(_ rgba: UnsafePointer<UInt8>, width: Int, height: Int) -> CGImage? {
        let data = Data(bytes: rgba, count: width * height * 4)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: true,
                       intent: .defaultIntent)
    }

    private static var icons: [Int: CGImage] = [:]
    private static var portraits: [Int: CGImage] = [:]

    /// A named intensity graphic from the game ("emblem", "melee") as a
    /// template image `height` points tall, tinted like text.
    static func template(_ name: String, height: CGFloat) -> NSImage? {
        var w: Int32 = 0, h: Int32 = 0
        guard GameAssets.shared.ready, let alpha = pet_named_alpha(name, &w, &h) else { return nil }
        let count = Int(w * h)
        var rgba = [UInt8](repeating: 255, count: count * 4)
        for p in 0..<count { rgba[p * 4 + 3] = alpha[p] }
        guard let cg = rgba.withUnsafeBufferPointer({
            cgImage($0.baseAddress!, width: Int(w), height: Int(h))
        }) else { return nil }
        let size = NSSize(width: height * CGFloat(w) / CGFloat(h), height: height)
        let image = NSImage(cgImage: cg, size: size)
        image.isTemplate = true
        return image
    }

    /// Face icon of roster entry `index` (Melee's select grid order).
    static func icon(_ index: Int) -> CGImage? {
        if let cached = icons[index] { return cached }
        var w: Int32 = 0, h: Int32 = 0
        guard let px = pet_roster_icon(Int32(index), &w, &h),
              let image = cgImage(px, width: Int(w), height: Int(h)) else { return nil }
        icons[index] = image
        return image
    }

    /// Character select portrait for a character kind and costume.
    static func portrait(ckind: Int, costume: Int) -> CGImage? {
        let key = ckind * 16 + costume
        if let cached = portraits[key] { return cached }
        var w: Int32 = 0, h: Int32 = 0
        guard let px = pet_portrait(Int32(ckind), Int32(costume), &w, &h),
              let image = cgImage(px, width: Int(w), height: Int(h)) else { return nil }
        portraits[key] = image
        return image
    }
}

// MARK: - Sounds

/// Melee's own menu sounds and announcer, played by the game's mixer.
enum MenuSound: Int32 {
    case back = 0, confirm = 1, move = 2, denied = 3

    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard

    /// The Sound tab's "Menu sounds" switch (on by default).
    static var enabled: Bool {
        get { defaults.object(forKey: "sound.menu") as? Bool ?? true }
        set { defaults.set(newValue, forKey: "sound.menu") }
    }

    func play() {
        guard MenuSound.enabled, GameAssets.shared.ready else { return }
        pet_sound_menu(rawValue)
    }

    /// The announcer calls out roster entry `index`'s name. Not during a
    /// match: the match swaps in its own sound banks.
    static func announce(rosterIndex index: Int) {
        guard enabled, GameAssets.shared.ready, !pet_match_active() else { return }
        pet_sound_announce(Int32(index))
    }
}

// MARK: - Font

/// Melee's text font (the sislib glyphs, 32x32 each, with per-glyph margins).
enum MeleeFont {
    struct Glyph {
        let image: CGImage
        let left: Int
        let right: Int
        var advance: Int { max(1, 32 - left - right) }
    }

    private static var glyphs: [Character: Glyph] = [:]
    private static var loaded = false

    private static func load() {
        guard !loaded else { return }
        let count = Int(pet_font_glyph_count())
        for i in 0..<count {
            var hi: UInt8 = 0, lo: UInt8 = 0
            var atlas: Int32 = 0, left: Int32 = 0, right: Int32 = 0
            guard pet_font_glyph_info(Int32(i), &hi, &lo, &atlas, &left, &right),
                  let sjis = String(data: Data([hi, lo]), encoding: .shiftJIS),
                  let alpha = pet_font_glyph_alpha(atlas) else { continue }
            // White glyphs whose alpha is the coverage: a template image.
            var rgba = [UInt8](repeating: 255, count: 32 * 32 * 4)
            for p in 0..<(32 * 32) { rgba[p * 4 + 3] = alpha[p] }
            guard let image = rgba.withUnsafeBufferPointer({
                GameImages.cgImage($0.baseAddress!, width: 32, height: 32)
            }) else { continue }
            let glyph = Glyph(image: image, left: Int(left), right: Int(right))
            // Full-width forms fold to ASCII ("Ａ" -> "A").
            for form in Set([sjis, sjis.precomposedStringWithCompatibilityMapping]) {
                if let c = form.first, form.count == 1, glyphs[c] == nil {
                    glyphs[c] = glyph
                }
            }
        }
        // Typographic forms in the glyph table for plain ASCII.
        let aliases: [Character: Character] = ["\"": "”", "'": "’", "-": "−", "\\": "¥", "~": "〜"]
        for (ascii, typo) in aliases where glyphs[ascii] == nil {
            glyphs[ascii] = glyphs[typo]
        }
        loaded = count > 0 && !glyphs.isEmpty
    }

    static var available: Bool {
        load()
        return loaded
    }

    /// The width of `text` set at `size`, in points.
    static func width(_ text: String, size: CGFloat, tracking: CGFloat = 0) -> CGFloat {
        load()
        let unit = size / 32
        let fallback = NSFont.systemFont(ofSize: size * 0.62, weight: .bold)
        var w: CGFloat = 0
        for c in text {
            if let g = glyphs[c] {
                w += CGFloat(g.advance) * unit
            } else {
                w += (String(c) as NSString).size(withAttributes: [.font: fallback]).width
            }
        }
        return w + tracking * CGFloat(max(0, text.count - 1))
    }

    private static var cache: [String: NSImage] = [:]

    /// `text` set in Melee's font, `size` points tall (one glyph cell), as a
    /// template image. Characters the font lacks fall back to the system
    /// font. Nil until the game has loaded its font.
    static func image(_ text: String, size: CGFloat, tracking: CGFloat = 0) -> NSImage? {
        load()
        guard loaded else { return nil }
        let key = "\(size)|\(tracking)|\(text)"
        if let cached = cache[key] { return cached }

        let unit = size / 32 // points per glyph pixel
        let fallback = NSFont.systemFont(ofSize: size * 0.62, weight: .bold)
        var pieces: [(Glyph?, String, CGFloat)] = [] // glyph or fallback text, width
        for c in text {
            if let g = glyphs[c] {
                pieces.append((g, "", CGFloat(g.advance) * unit))
            } else {
                let s = String(c)
                let w = (s as NSString).size(withAttributes: [.font: fallback]).width
                pieces.append((nil, s, w))
            }
        }
        let width = max(1, pieces.reduce(0) { $0 + $1.2 } + tracking * CGFloat(max(0, pieces.count - 1)))
        let image = NSImage(size: NSSize(width: ceil(width), height: size), flipped: false) { _ in
            guard let ctx = NSGraphicsContext.current?.cgContext else { return false }
            ctx.interpolationQuality = .high
            var x: CGFloat = 0
            for (glyph, s, w) in pieces {
                if let g = glyph {
                    // The glyph cell starts `left` pixels before the ink.
                    let rect = CGRect(x: x - CGFloat(g.left) * unit, y: 0, width: size, height: size)
                    ctx.draw(g.image, in: rect)
                } else {
                    (s as NSString).draw(at: NSPoint(x: x, y: size * 0.18),
                                         withAttributes: [.font: fallback, .foregroundColor: NSColor.black])
                }
                x += w + tracking
            }
            return true
        }
        image.isTemplate = true
        cache[key] = image
        return image
    }
}

/// Text in Melee's font, tinted by the foreground style like `Text`. Falls back
/// to the system font until the game has loaded its font.
struct MText: View {
    let text: String
    var size: CGFloat = 16
    var tracking: CGFloat = 0
    @ObservedObject private var assets = GameAssets.shared

    init(_ text: String, size: CGFloat = 16, tracking: CGFloat = 0) {
        self.text = text
        self.size = size
        self.tracking = tracking
    }

    var body: some View {
        if assets.ready, let image = MeleeFont.image(text, size: size, tracking: tracking) {
            Image(nsImage: image)
                .renderingMode(.template)
                .accessibilityLabel(text)
        } else {
            Text(text).font(.system(size: size * 0.6, weight: .bold))
        }
    }
}

/// Several lines of Melee's font, wrapped at word boundaries to `width`.
struct MParagraph: View {
    let text: String
    var size: CGFloat = 16
    var width: CGFloat
    @ObservedObject private var assets = GameAssets.shared

    init(_ text: String, size: CGFloat = 16, width: CGFloat) {
        self.text = text
        self.size = size
        self.width = width
    }

    private var lines: [String] {
        var lines: [String] = []
        var line = ""
        for word in text.split(separator: " ") {
            let candidate = line.isEmpty ? String(word) : line + " " + word
            if !line.isEmpty && MeleeFont.width(candidate, size: size) > width {
                lines.append(line)
                line = String(word)
            } else {
                line = candidate
            }
        }
        if !line.isEmpty { lines.append(line) }
        return lines
    }

    var body: some View {
        if assets.ready && MeleeFont.available {
            VStack(alignment: .leading, spacing: -size * 0.04) {
                ForEach(Array(lines.enumerated()), id: \.offset) { _, line in
                    MText(line, size: size)
                }
            }
        } else {
            Text(text)
                .font(.system(size: size * 0.6))
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

// MARK: - Readiness

/// Becomes ready when the game has decoded its assets (first idle scene).
final class GameAssets: ObservableObject {
    static let shared = GameAssets()
    @Published private(set) var ready = false

    fileprivate func markReady() {
        ready = true
    }
}

/// Called from C once the roster and font are available.
@_cdecl("pet_ui_assets_ready")
public func petUIAssetsReady() {
    GameAssets.shared.markReady()
    if let dir = ProcessInfo.processInfo.environment["MELEE_PET_DUMP"] {
        // The game calls this on the main thread.
        MainActor.assumeIsolated { dumpSamples(to: dir) }
    }
}

/// MELEE_PET_DUMP: render sample text in Melee's font, to check it.
@MainActor
private func dumpSamples(to dir: String) {
    let samples = ["MATCH  PLAYERS  CONTROLS  SOUND  DISPLAY",
                   "Start Match  Stocks 4  Time 7:00  100%",
                   "Fox  Falco  Marth  Jigglypuff  Mr. Game & Watch",
                   "!?\"'-~ the quick brown fox jumps over the lazy dog"]
    for (i, s) in samples.enumerated() {
        guard let image = MeleeFont.image(s, size: 32),
              let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { continue }
        try? png.write(to: URL(fileURLWithPath: "\(dir)/font_sample_\(i).png"))
    }
    // The popover itself, rendered offscreen (no frosted background). One
    // DisplaySettings: each one re-sends the screen choice to the game.
    let dumpDisplay = DisplaySettings()
    for tab in [PetTab.match, .players, .controls, .sound, .display] {
        let panel = PetPanel(audio: AudioSettings(), match: MatchSettings(),
                             players: PlayerSettings(), controls: ControlsSettings(),
                             display: dumpDisplay,
                             initialTab: tab, scrolls: false)
            .background(Color(white: 0.16))
            .environment(\.colorScheme, .dark)
        let renderer = ImageRenderer(content: panel)
        renderer.scale = 2
        guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { continue }
        try? png.write(to: URL(fileURLWithPath: "\(dir)/panel_\(tab.rawValue).png"))
    }
}
