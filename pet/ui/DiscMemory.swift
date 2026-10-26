// SPDX-License-Identifier: GPL-3.0-or-later
// Which disc Desktop Melee boots: only one that passed the disc check
// (pet/src/disc_check.cpp). A passed check is remembered per file, by its
// path, size and modification date, so it runs once.
import AppKit
import PetBridge

enum DiscMemory {
    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard
    private static let verifiedKey = "disc.verified"

    /// Set once the player has been through the welcome window.
    static var onboardingDone: Bool {
        get { defaults.bool(forKey: "onboarding.done") }
        set { defaults.set(newValue, forKey: "onboarding.done") }
    }

    private static func fingerprint(_ path: String) -> (size: Int, mtime: Double)? {
        guard let attrs = try? FileManager.default.attributesOfItem(atPath: path),
              (attrs[.type] as? FileAttributeType) == .typeRegular,
              let size = (attrs[.size] as? NSNumber)?.intValue,
              let date = attrs[.modificationDate] as? Date else { return nil }
        return (size, date.timeIntervalSince1970)
    }

    /// True when this exact file (same path, size and date) passed the check.
    static func isVerified(_ path: String) -> Bool {
        guard let saved = defaults.dictionary(forKey: verifiedKey),
              saved["path"] as? String == path,
              let now = fingerprint(path) else { return false }
        return (saved["size"] as? NSNumber)?.intValue == now.size
            && (saved["mtime"] as? NSNumber)?.doubleValue == now.mtime
    }

    static func remember(_ path: String) {
        guard let now = fingerprint(path) else { return }
        defaults.set(["path": path, "size": now.size, "mtime": now.mtime], forKey: verifiedKey)
    }
}

/// Runs the disc check to the end on this thread (the stand-in picker only).
private func checkDisc(_ path: String) -> (PetDiscState, String) {
    let check = pet_disc_check_start(path)
    defer { pet_disc_check_free(check) }
    var message = [CChar](repeating: 0, count: 512)
    var percent: Int32 = 0
    while true {
        let state = pet_disc_check_poll(check, &percent, &message, Int32(message.count))
        if state != PET_DISC_CHECKING {
            return (state, String(cString: message))
        }
        Thread.sleep(forTimeInterval: 0.1)
    }
}

private var chosenDisc: UnsafeMutablePointer<CChar>?

private func choose(_ path: String) -> UnsafePointer<CChar>? {
    free(chosenDisc)
    chosenDisc = strdup(path)
    return UnsafePointer(chosenDisc)
}

/// Called from C (pet_choose_disc) before the game boots: the disc to boot,
/// or nil when the player gave up. `candidate` is the command line's disc,
/// else the one booted last time.
@_cdecl("pet_ui_choose_disc")
public func petUIChooseDisc(_ candidate: UnsafePointer<CChar>?) -> UnsafePointer<CChar>? {
    var path = candidate.map { String(cString: $0) } ?? ""
    if !path.isEmpty, DiscMemory.isVerified(path) {
        NSLog("pet: disc %@ (remembered)", path)
        return choose(path)
    }
    // Stand-in until the welcome window: check the candidate, then a picker.
    while true {
        if !path.isEmpty {
            let (state, message) = checkDisc(path)
            if state == PET_DISC_VERIFIED {
                NSLog("pet: disc %@ (checked)", path)
                DiscMemory.remember(path)
                return choose(path)
            }
            NSLog("pet: disc %@ (refused: %@)", path, message)
            NSApp.activate(ignoringOtherApps: true)
            let alert = NSAlert()
            alert.messageText = "This disc can't be used"
            alert.informativeText = message
            alert.runModal()
        }
        let panel = NSOpenPanel()
        panel.title = "Choose your Melee disc"
        panel.message = "Super Smash Bros. Melee (USA, v1.02) disc image: .iso, .gcm, .ciso or .rvz"
        panel.prompt = "Play"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        NSApp.activate(ignoringOtherApps: true)
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        path = url.path
    }
}
