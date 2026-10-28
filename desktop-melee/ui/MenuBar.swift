// SPDX-License-Identifier: GPL-3.0-or-later
// The menu bar app: a Smash emblem in the menu bar that opens the Desktop Melee
// popover. Runs inside the game process, on the main thread (the game loop's
// thread, which pumps AppKit events every frame through SDL).
import AppKit
import Combine
import DesktopMeleeBridge
import SwiftUI

final class MenuBarController: NSObject, NSPopoverDelegate {
    static var shared: MenuBarController?

    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    private let popover = NSPopover()
    private let audio = AudioSettings()
    private let match = MatchSettings()
    private let players = PlayerSettings()
    private let controls = ControlsSettings()
    private let display = DisplaySettings()
    private weak var gameWindow: NSWindow?
    private var assetsReady: AnyCancellable?

    init(gameWindow: NSWindow?) {
        self.gameWindow = gameWindow
        super.init()
        if let button = statusItem.button {
            button.image = SmashEmblem.image(size: 18)
            button.image?.accessibilityDescription = "Desktop Melee"
            button.target = self
            button.action = #selector(togglePopover(_:))
        }
        popover.behavior = .transient
        popover.animates = true
        popover.delegate = self
        let hosting = NSHostingController(rootView: DesktopMeleePanel(
            audio: audio, match: match, players: players, controls: controls, display: display))
        // Size the popover from SwiftUI's layout, so it is placed for its real
        // height (otherwise the top can end up above the screen).
        hosting.sizingOptions = [.preferredContentSize]
        popover.contentViewController = hosting
        // Swap the stand-in emblem for Melee's own once the game has loaded it.
        // (@Published announces before the value changes, so build it after.)
        assetsReady = GameAssets.shared.$ready.sink { [weak self] ready in
            guard ready else { return }
            DispatchQueue.main.async {
                if let emblem = GameImages.template("emblem", height: 17) {
                    emblem.accessibilityDescription = "Desktop Melee"
                    self?.statusItem.button?.image = emblem
                }
            }
        }
    }

    @objc private func togglePopover(_ sender: Any?) {
        if popover.isShown {
            popover.performClose(sender)
            return
        }
        guard let button = statusItem.button else { return }
        audio.load()
        NSApp.activate(ignoringOtherApps: true)
        if let view = popover.contentViewController?.view {
            view.layoutSubtreeIfNeeded()
            popover.contentSize = view.fittingSize
        }
        popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        if let window = popover.contentViewController?.view.window {
            // The game overlay sits above the menu bar; keep the popover above it.
            window.level = .popUpMenu
            window.makeKey()
        }
    }

    func logStatus() {
        let frame = statusItem.button?.window?.frame ?? .zero
        NSLog("desktop-melee: menu bar item %@ at %@ (visible %d)",
              statusItem.isVisible ? "shown" : "hidden",
              NSStringFromRect(frame), statusItem.button?.window?.isVisible == true ? 1 : 0)
    }

    /// Keyboard focus to the game window: melee-pc ignores keys while it
    /// isn't the key window (src/pc/keyboard.c).
    func focusGame() {
        gameWindow?.makeKey()
    }

    /// Posted when the popover closes (the shortcut recorder stops).
    static let popoverClosed = Notification.Name("DesktopMeleePopoverClosed")

    func popoverDidClose(_ notification: Notification) {
        NotificationCenter.default.post(name: Self.popoverClosed, object: nil)
        // Hand keyboard focus back to the game so keyboard play keeps working.
        gameWindow?.makeKey()
    }
}

/// Called from C (Pause and hide's Show): the keyboard back to the game.
@_cdecl("desktop_melee_ui_focus_game")
public func desktopMeleeUIFocusGame() {
    NSApp.activate(ignoringOtherApps: true)
    MenuBarController.shared?.focusGame()
}

/// Called from C once the game window exists (desktop-melee/src/overlay_mac.m).
@_cdecl("desktop_melee_ui_install")
public func desktopMeleeUIInstall(_ gameWindow: UnsafeMutableRawPointer?) {
    // A menu bar app: no Dock icon (and the Dock, Fox's floor, keeps its width).
    NSApp.setActivationPolicy(.accessory)
    let window = gameWindow.map { Unmanaged<NSWindow>.fromOpaque($0).takeUnretainedValue() }
    let controller = MenuBarController(gameWindow: window)
    MenuBarController.shared = controller
    PauseHotkey.install()
    // Start on a controller: only if the Match tab turned it on.
    StartButton.apply()
    // Accessory apps are not made active at launch; do it so the game window
    // gets keyboard focus (melee-pc ignores keys while it is unfocused).
    NSApp.activate(ignoringOtherApps: true)
    window?.makeKeyAndOrderFront(nil)
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { controller.logStatus() }
}
