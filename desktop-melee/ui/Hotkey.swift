// SPDX-License-Identifier: GPL-3.0-or-later
// Pause and hide's global shortcut (Control-Option-M by default). A Carbon
// system hotkey: it works while any app is in front and needs no
// Accessibility permission.
import AppKit
import Carbon
import DesktopMeleeBridge

enum PauseHotkey {
    private static var defaults: UserDefaults { Settings.defaults }
    private static let key = "match.pauseKey"
    static let defaultKeyCode = UInt32(kVK_ANSI_M)
    static let defaultMods = UInt32(controlKey | optionKey)

    private static var hotKey: EventHotKeyRef?
    private static var handler: EventHandlerRef?

    /// Fires when the shortcut changes (the Match tab shows it).
    static let changed = Notification.Name("PauseHotkeyChanged")

    private static var stored: [String: Any] { defaults.dictionary(forKey: key) ?? [:] }
    static var keyCode: UInt32 { (stored["key"] as? NSNumber)?.uint32Value ?? defaultKeyCode }
    static var mods: UInt32 { (stored["mods"] as? NSNumber)?.uint32Value ?? defaultMods }
    private static var label: String { stored["label"] as? String ?? "M" }

    /// The shortcut as text, e.g. "⌃⌥M".
    static var display: String {
        var s = ""
        if mods & UInt32(controlKey) != 0 { s += "⌃" }
        if mods & UInt32(optionKey) != 0 { s += "⌥" }
        if mods & UInt32(shiftKey) != 0 { s += "⇧" }
        if mods & UInt32(cmdKey) != 0 { s += "⌘" }
        return s + label
    }

    /// Once, when the menu bar app starts.
    static func install() {
        guard handler == nil else { return }
        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        InstallEventHandler(GetApplicationEventTarget(), { _, _, _ in
            desktop_melee_toggle_pause_and_hide()
            return noErr
        }, 1, &spec, nil, &handler)
        _ = register(keyCode: keyCode, mods: mods)
        NSLog("desktop-melee: Pause and hide shortcut %@ %@", display,
              hotKey != nil ? "registered" : "taken")
    }

    /// Makes this the system hotkey; false (and none registered) when
    /// another app has it.
    private static func register(keyCode: UInt32, mods: UInt32) -> Bool {
        if let hotKey {
            UnregisterEventHotKey(hotKey)
            self.hotKey = nil
        }
        let id = EventHotKeyID(signature: OSType(0x444D_454C), id: 1) // "DMEL"
        let status = RegisterEventHotKey(keyCode, mods, id, GetApplicationEventTarget(), 0, &hotKey)
        if status != noErr { hotKey = nil }
        return status == noErr
    }

    /// A new shortcut from the recorder: active and saved right away, or
    /// false when another app already has it (the old one stays).
    static func set(keyCode: UInt32, mods: UInt32, label: String) -> Bool {
        guard register(keyCode: keyCode, mods: mods) else {
            _ = register(keyCode: self.keyCode, mods: self.mods)
            NSLog("desktop-melee: Pause and hide shortcut not changed: taken by another app")
            return false
        }
        defaults.set(["key": keyCode, "mods": mods, "label": label], forKey: key)
        NotificationCenter.default.post(name: changed, object: nil)
        return true
    }

    /// Carbon modifiers from a key event's; nil unless there are at least two
    /// modifiers, one of them Control, Option or Command, so the shortcut
    /// can't take an everyday combo (Command-C) from every app.
    static func carbonMods(_ flags: NSEvent.ModifierFlags) -> UInt32? {
        var m: UInt32 = 0
        var count = 0
        if flags.contains(.control) { m |= UInt32(controlKey); count += 1 }
        if flags.contains(.option) { m |= UInt32(optionKey); count += 1 }
        if flags.contains(.command) { m |= UInt32(cmdKey); count += 1 }
        guard m != 0 else { return nil }
        if flags.contains(.shift) { m |= UInt32(shiftKey); count += 1 }
        return count >= 2 ? m : nil
    }

    /// A short name for the key itself.
    static func label(for event: NSEvent) -> String {
        switch Int(event.keyCode) {
        case kVK_Space: return "Space"
        case kVK_Return: return "↩"
        case kVK_Tab: return "⇥"
        case kVK_Delete: return "⌫"
        default:
            if let c = event.charactersIgnoringModifiers?.uppercased(), c.count == 1,
               let scalar = c.unicodeScalars.first, scalar.value >= 0x21, scalar.value < 0x7F {
                return c
            }
        }
        return "Key \(event.keyCode)"
    }
}
