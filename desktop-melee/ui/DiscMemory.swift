// SPDX-License-Identifier: GPL-3.0-or-later
// Which disc Desktop Melee boots: only one that passed the disc check
// (desktop-melee/src/disc_check.cpp). A passed check is remembered per file, by its
// path, size and modification date, so it runs once.
import AppKit
import DesktopMeleeBridge

enum DiscMemory {
    private static var defaults: UserDefaults { Settings.defaults }
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

private var chosenDisc: UnsafeMutablePointer<CChar>?

private func choose(_ path: String) -> UnsafePointer<CChar>? {
    free(chosenDisc)
    chosenDisc = strdup(path)
    return UnsafePointer(chosenDisc)
}

/// Called from C (desktop_melee_choose_disc) before the game boots: the disc to boot,
/// or nil when the player gave up. `candidate` is the command line's disc,
/// else the one booted last time.
@_cdecl("desktop_melee_ui_choose_disc")
public func desktopMeleeUIChooseDisc(_ candidate: UnsafePointer<CChar>?) -> UnsafePointer<CChar>? {
    let path = candidate.map { String(cString: $0) } ?? ""
    // DESKTOP_MELEE_WELCOME=1 or =disc: the whole welcome again (testing).
    // =debug boots as usual and opens the debug window once the game is up.
    let welcome = ProcessInfo.processInfo.environment["DESKTOP_MELEE_WELCOME"]
    let firstRun = !DiscMemory.onboardingDone || (welcome != nil && welcome != "debug")
    if !firstRun, !path.isEmpty, DiscMemory.isVerified(path) {
        NSLog("desktop-melee: disc %@ (remembered)", path)
        return choose(path)
    }
    guard let chosen = WelcomeController.shared.runDiscPhase(candidate: path, firstRun: firstRun)
    else { return nil }
    return choose(chosen)
}
