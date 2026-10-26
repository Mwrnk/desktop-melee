// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window (pet/onboarding-design.md): what Desktop Melee is, the
// player's own disc (checked, only a clean USA 1.02 passes), then how to
// play. Steps 1-2 run modally before the game boots, from pet_choose_disc.
import AppKit
import PetBridge
import SwiftUI
import UniformTypeIdentifiers

enum WelcomeStep: Int, CaseIterable {
    case welcome = 1, disc, controls, permission, ready
}

enum DiscStatus: Equatable {
    case empty
    case checking(path: String, percent: Int)
    case verified(path: String)
    case failed(path: String, message: String)
}

final class WelcomeController: NSObject, ObservableObject, NSWindowDelegate {
    static let shared = WelcomeController()

    @Published var step: WelcomeStep = .welcome
    @Published var disc: DiscStatus = .empty
    /// The disc step after the first run: the saved disc moved or changed.
    @Published var findAgain = false
    @Published var oldPath: String?
    /// An offscreen sample for MELEE_PET_DUMP renders, not the live window.
    var isSample = false

    private var window: NSWindow?
    private var check: OpaquePointer?
    private var timer: Timer?
    private var candidate: String?
    private var result: String?
    private var modal = false

    // MARK: Phase 1

    /// Shows steps 1-2 (or only the disc step when !firstRun) and returns the
    /// verified disc, or nil when the player closed the window.
    func runDiscPhase(candidate: String?, firstRun: Bool) -> String? {
        self.candidate = candidate.flatMap { $0.isEmpty ? nil : $0 }
        result = nil
        disc = .empty
        findAgain = !firstRun
        oldPath = firstRun ? nil : self.candidate
        step = firstRun ? .welcome : .disc
        if step == .disc { startCandidateCheck() }
        let window = makeWindow()
        MainActor.assumeIsolated { dumpPhaseOne() }
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.makeKeyAndOrderFront(nil)
        modal = true
        NSApp.runModal(for: window)
        modal = false
        return result
    }

    func goTo(_ next: WelcomeStep) {
        step = next
        if next == .disc, disc == .empty { startCandidateCheck() }
    }

    /// The saved disc, checked as soon as the disc step shows.
    private func startCandidateCheck() {
        if let path = candidate, FileManager.default.fileExists(atPath: path) {
            startCheck(path)
        }
    }

    func chooseFile() {
        let panel = NSOpenPanel()
        panel.message = "Choose your Super Smash Bros. Melee disc image (USA, 1.02)"
        panel.prompt = "Choose"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = ["iso", "gcm", "ciso", "rvz"].compactMap {
            UTType(filenameExtension: $0)
        }
        panel.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        guard let window else { return }
        panel.beginSheetModal(for: window) { [weak self] response in
            if response == .OK, let url = panel.url { self?.startCheck(url.path) }
        }
    }

    /// A file dropped on the window.
    func drop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier)
        }) else { return false }
        _ = provider.loadObject(ofClass: URL.self) { [weak self] url, _ in
            guard let url else { return }
            DispatchQueue.main.async { self?.startCheck(url.path) }
        }
        return true
    }

    func startCheck(_ path: String) {
        stopCheck()
        disc = .checking(path: path, percent: 0)
        check = pet_disc_check_start(path)
        let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in self?.pollCheck() }
        // .common includes the modal panel mode runModal(for:) runs in.
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    private func pollCheck() {
        guard let check, case .checking(let path, _) = disc else { return }
        var message = [CChar](repeating: 0, count: 512)
        var percent: Int32 = 0
        let state = pet_disc_check_poll(check, &percent, &message, Int32(message.count))
        switch state {
        case PET_DISC_CHECKING:
            disc = .checking(path: path, percent: Int(percent))
        case PET_DISC_VERIFIED:
            NSLog("pet: disc %@ (checked)", path)
            stopCheck()
            disc = .verified(path: path)
        default:
            let text = String(cString: message)
            NSLog("pet: disc %@ (refused: %@)", path, text)
            stopCheck()
            disc = .failed(path: path, message: text)
        }
    }

    private func stopCheck() {
        timer?.invalidate()
        timer = nil
        if let check {
            pet_disc_check_free(check)
            self.check = nil
        }
    }

    /// Continue on the disc step: boot this disc.
    func acceptDisc() {
        guard case .verified(let path) = disc else { return }
        DiscMemory.remember(path)
        result = path
        modal = false
        NSApp.stopModal()
        window?.orderOut(nil)
    }

    // MARK: Window

    private func makeWindow() -> NSWindow {
        if let window { return window }
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 560),
                              styleMask: [.titled, .closable, .fullSizeContentView],
                              backing: .buffered, defer: false)
        window.title = "Desktop Melee"
        window.titlebarAppearsTransparent = true
        window.titleVisibility = .hidden
        window.isMovableByWindowBackground = true
        window.isReleasedWhenClosed = false
        window.appearance = NSAppearance(named: .darkAqua)
        // The game overlay sits just below the menu bar; stay above it.
        window.level = .popUpMenu
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        window.delegate = self
        window.contentView = NSHostingView(rootView: WelcomeView(controller: self))
        self.window = window
        return window
    }

    func windowWillClose(_ notification: Notification) {
        stopCheck()
        if modal {
            // Closed before a disc was accepted: the game quits.
            result = nil
            modal = false
            NSApp.stopModal()
        }
    }

    // MARK: Dumps (MELEE_PET_DUMP)

    @MainActor
    private func dumpPhaseOne() {
        guard let dir = ProcessInfo.processInfo.environment["MELEE_PET_DUMP"] else { return }
        let samples: [(String, WelcomeStep, DiscStatus, Bool)] = [
            ("welcome_1", .welcome, .empty, false),
            ("welcome_2_empty", .disc, .empty, false),
            ("welcome_2_checking", .disc, .checking(path: "/Users/you/Downloads/Melee.iso", percent: 62), false),
            ("welcome_2_verified", .disc, .verified(path: "/Users/you/Downloads/Melee.iso"), false),
            ("welcome_2_failed", .disc, .failed(path: "/Users/you/Downloads/Melee PAL.iso",
                                                message: "This is the European (PAL) disc. Desktop Melee needs the USA version, 1.02."), false),
            ("welcome_2_again", .disc, .empty, true),
        ]
        for (name, step, disc, again) in samples {
            let sample = WelcomeController()
            sample.isSample = true
            sample.step = step
            sample.disc = disc
            sample.findAgain = again
            sample.oldPath = again ? "/Users/you/Downloads/Melee.iso" : nil
            WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/\(name).png")
        }
    }

    @MainActor
    static func dump<V: View>(_ view: V, to path: String) {
        let renderer = ImageRenderer(content: view.environment(\.colorScheme, .dark))
        renderer.scale = 2
        guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { return }
        try? png.write(to: URL(fileURLWithPath: path))
    }
}
