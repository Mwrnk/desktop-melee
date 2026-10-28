// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window: what Desktop Melee is, the
// player's own disc (checked, only a clean USA 1.02 passes), then how to
// play. Steps 1-2 run modally before the game boots, from desktop_melee_choose_disc.
import AppKit
import Combine
import DesktopMeleeBridge
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
    /// An offscreen sample for DESKTOP_MELEE_DUMP renders, not the live window.
    var isSample = false
    /// The debug window (DESKTOP_MELEE_WELCOME=debug): live, but nothing it
    /// does reaches the game or the saved settings.
    var debug = false
    /// Which way the last step change went, for the slide.
    @Published var forward = true
    /// Bumped to play the current step's entrance again (debug).
    @Published var replay = 0
    /// Bumped on each refused disc: the status row shakes.
    @Published var failures = 0
    /// The graphics build the loading screen shows.
    var progress: ShaderProgress = .shared
    /// Phase 2: the disc is in and the game is booting ("Getting Melee ready").
    @Published var loading = false
    /// Step 3: who pressed a button, by controller port.
    @Published var pressed: [PortPress] = []
    @Published var keyboardOpen = false

    struct PortPress: Identifiable, Equatable {
        let port: Int
        let name: String
        var id: Int { port }
    }

    var firstRun = false
    private var assetsReady: AnyCancellable?
    private var padTimer: Timer?
    private var lastButtons = [UInt32](repeating: 0, count: 4)

    private var window: NSWindow?
    private var check: OpaquePointer?
    private var timer: Timer?
    private var candidate: String?
    private var result: String?
    private var modal = false
    /// Steps 1-2 are up, before any disc is in (the small loading window
    /// stays away meanwhile).
    var waitingForDisc: Bool { modal }

    // MARK: Phase 1

    /// Shows steps 1-2 (or only the disc step when !firstRun) and returns the
    /// verified disc, or nil when the player closed the window.
    func runDiscPhase(candidate: String?, firstRun: Bool) -> String? {
        self.candidate = candidate.flatMap { $0.isEmpty ? nil : $0 }
        self.firstRun = firstRun
        result = nil
        disc = .empty
        findAgain = !firstRun
        oldPath = firstRun ? nil : self.candidate
        step = firstRun ? .welcome : .disc
        // DESKTOP_MELEE_WELCOME=disc: open on the disc step (testing).
        if ProcessInfo.processInfo.environment["DESKTOP_MELEE_WELCOME"] == "disc" { step = .disc }
        if step == .disc { startCandidateCheck() }
        let window = makeWindow()
        MainActor.assumeIsolated { dumpPhaseOne() }
        // Until a disc is in, the overlay has nothing to show: hide it, so
        // the whole desktop stays the player's.
        desktop_melee_mac_overlay_set_hidden(true)
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.makeKeyAndOrderFront(nil)
        modal = true
        NSApp.runModal(for: window)
        modal = false
        desktop_melee_mac_overlay_set_hidden(false)
        if result != nil, loading {
            beginPhaseTwo(window)
        } else {
            MenuBarController.shared?.focusGame()
        }
        return result
    }

    func goTo(_ next: WelcomeStep) {
        // Accessibility already granted: nothing to ask.
        if next == .permission, !debug, desktop_melee_ax_trusted() {
            goTo(.ready)
            return
        }
        forward = next.rawValue >= step.rawValue
        step = next
        if next == .disc, disc == .empty { startCandidateCheck() }
        if next == .controls {
            if !debug { desktop_melee_set_start_starts_match(false) }
            if padTimer == nil { startPadPolling() }
        }
        if next == .permission { watchAccessibility() }
        if next == .ready, !debug {
            DiscMemory.onboardingDone = true
            // "Or press Start on your controller."
            desktop_melee_set_start_starts_match(true)
        }
    }

    /// Back: not to the disc once it is in (the game has booted with it), and
    /// not past where the window opened.
    var canGoBack: Bool {
        switch step {
        case .disc: return firstRun && !findAgain && !loading
        case .permission, .ready: return true
        default: return false
        }
    }

    func goBack() {
        MenuSound.back.play()
        switch step {
        case .disc: goTo(.welcome)
        case .permission: goTo(.controls)
        // Accessibility granted: its step is skipped going forward, so too back.
        case .ready: goTo(!debug && desktop_melee_ax_trusted() ? .controls : .permission)
        default: break
        }
    }

    // MARK: Step 5, and How to play

    /// Start a match (the Match tab's Start) and close the window.
    func startMatch() {
        if debug {
            MenuSound.confirm.play()
            NSLog("desktop-melee: welcome debug: Start a match (not started)")
            return
        }
        DiscMemory.onboardingDone = true
        desktop_melee_request_match()
        window?.close()
    }

    /// The menu bar's How to play: the window again, at `at` (the game is up).
    func showAgain(at next: WelcomeStep) {
        let window = makeWindow()
        loading = false
        firstRun = false
        pressed = []
        keyboardOpen = false
        axGranted = false
        window.level = .popUpMenu
        goTo(next)
        if step == .controls {
            desktop_melee_set_start_starts_match(false)
            startPadPolling()
        }
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.orderFrontRegardless()
        MenuBarController.shared?.focusGame()
    }

    // MARK: Step 4

    @Published var axGranted = false
    private var axTimer: Timer?

    /// Open System Settings: the system dialog, then watch for the switch.
    func requestAccessibility() {
        if debug {
            // No system dialog: the switch is turned on a moment later.
            let delay = 1.2 / WelcomeMotion.shared.speed
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                if self?.step == .permission { self?.accessibilityGranted() }
            }
            return
        }
        desktop_melee_ax_request()
        watchAccessibility()
    }

    private func watchAccessibility() {
        guard !debug, axTimer == nil else { return }
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in self?.checkAccessibility() }
        RunLoop.main.add(timer, forMode: .common)
        axTimer = timer
    }

    private func checkAccessibility() {
        guard step == .permission, desktop_melee_ax_trusted() else { return }
        accessibilityGranted()
    }

    func accessibilityGranted() {
        axTimer?.invalidate()
        axTimer = nil
        axGranted = true
        // The Dock's exact size, right away: no relaunch.
        if !debug { desktop_melee_relayout() }
        MenuSound.confirm.play()
        let delay = 0.8 / WelcomeMotion.shared.speed
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            if self?.step == .permission { self?.goTo(.ready) }
        }
    }

    /// The saved disc, checked as soon as the disc step shows.
    private func startCandidateCheck() {
        guard let path = candidate, FileManager.default.fileExists(atPath: path) else { return }
        if DiscMemory.isVerified(path) {
            // Already checked, and unchanged: no need to hash it again.
            NSLog("desktop-melee: disc %@ (remembered)", path)
            disc = .verified(path: path)
        } else {
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
        check = desktop_melee_disc_check_start(path)
        let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in self?.pollCheck() }
        // .common includes the modal panel mode runModal(for:) runs in.
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    private func pollCheck() {
        guard let check, case .checking(let path, _) = disc else { return }
        var message = [CChar](repeating: 0, count: 512)
        var percent: Int32 = 0
        let state = desktop_melee_disc_check_poll(check, &percent, &message, Int32(message.count))
        switch state {
        case DESKTOP_MELEE_DISC_CHECKING:
            disc = .checking(path: path, percent: Int(percent))
        case DESKTOP_MELEE_DISC_VERIFIED:
            NSLog("desktop-melee: disc %@ (checked)", path)
            stopCheck()
            disc = .verified(path: path)
        default:
            let text = String(cString: message)
            NSLog("desktop-melee: disc %@ (refused: %@)", path, text)
            stopCheck()
            disc = .failed(path: path, message: text)
            failures += 1
        }
    }

    private func stopCheck() {
        timer?.invalidate()
        timer = nil
        if let check {
            desktop_melee_disc_check_free(check)
            self.check = nil
        }
    }

    /// Continue on the disc step: boot this disc.
    func acceptDisc() {
        guard case .verified(let path) = disc else { return }
        if debug {
            simulateLoading()
            return
        }
        DiscMemory.remember(path)
        // Anything after the disc counts as done: closing, quitting or a
        // crash later on must not bring the whole welcome back.
        DiscMemory.onboardingDone = true
        result = path
        modal = false
        // The first run goes on to how to play once the game is up.
        loading = firstRun
        NSApp.stopModal()
        if !firstRun { window?.orderOut(nil) }
    }

    // MARK: Phase 2

    /// The game boots under "Getting Melee ready"; once its assets (emblem,
    /// sounds) are decoded, the window comes alive on step 3.
    private func beginPhaseTwo(_ window: NSWindow) {
        // Not modal any more: stay above the game overlay.
        window.level = .popUpMenu
        // "Press any button" must not start a match underneath.
        desktop_melee_set_start_starts_match(false)
        assetsReady = GameAssets.shared.$ready.sink { [weak self] ready in
            guard ready else { return }
            // @Published announces before the value changes.
            DispatchQueue.main.async { self?.reveal() }
        }
    }

    private func reveal() {
        guard loading else { return }
        assetsReady = nil
        // Debug: the disc's look arrives now, as it does for real.
        if debug { GameAssets.shared.debugBeforeDisc = false }
        loading = false
        forward = true
        step = .controls
        MenuSound.confirm.play()
        startPadPolling()
        // Keys go to the game, so a keyboard player's presses show up too;
        // the welcome panel still takes clicks without being key.
        MenuBarController.shared?.focusGame()
    }

    private func startPadPolling() {
        padTimer?.invalidate()
        lastButtons = (0..<4).map { desktop_melee_pad_buttons(Int32($0)) }
        let timer = Timer(timeInterval: 1.0 / 30, repeats: true) { [weak self] _ in self?.pollPads() }
        RunLoop.main.add(timer, forMode: .common)
        padTimer = timer
    }

    private func pollPads() {
        guard step == .controls else { return }
        for port in 0..<4 {
            let held = desktop_melee_pad_buttons(Int32(port))
            let newlyPressed = held & ~lastButtons[port]
            lastButtons[port] = held
            guard newlyPressed != 0, !pressed.contains(where: { $0.port == port }) else { continue }
            let device = String(cString: desktop_melee_port_device(Int32(port)))
            let name = device.isEmpty ? (port == 0 ? "Keyboard" : "Controller") : device
            pressed.append(PortPress(port: port, name: name))
            pressed.sort { $0.port < $1.port }
            MenuSound.move.play()
        }
    }

    /// Which device card a press lights up.
    enum DeviceKind { case gameCube, otherPad, keyboard }

    static func kind(of name: String) -> DeviceKind {
        if name == "Keyboard" { return .keyboard }
        if name.localizedCaseInsensitiveContains("GameCube") || name.localizedCaseInsensitiveContains("WaveBird") {
            return .gameCube
        }
        return .otherPad
    }

    private func endPhaseTwo() {
        padTimer?.invalidate()
        padTimer = nil
        axTimer?.invalidate()
        axTimer = nil
        assetsReady = nil
        if !debug { desktop_melee_set_start_starts_match(true) }
    }

    // MARK: Debug (DESKTOP_MELEE_WELCOME=debug)

    /// The debug window's own controller, kept while the panel is up.
    static var debugController: WelcomeController?

    /// Opens a live welcome window that touches nothing (see `debug`), next to
    /// a panel that jumps between steps and states and tunes the motion.
    static func openDebug() {
        let controller = debugController ?? WelcomeController()
        controller.debug = true
        controller.firstRun = true
        debugController = controller
        NSLog("desktop-melee: welcome debug window open")
        controller.showDebugWindow()
        WelcomeDebugPanel.show(for: controller)
    }

    func showDebugWindow() {
        let window = makeWindow()
        NSApp.activate(ignoringOtherApps: true)
        if !window.isVisible { window.center() }
        window.orderFrontRegardless()
    }

    var debugWindowFrame: NSRect? { window?.frame }

    /// The loading screen, with a made-up graphics build, then the reveal.
    func simulateLoading(seconds: Double = 6) {
        let speed = WelcomeMotion.shared.speed
        let progress = ShaderProgress()
        progress.simulate(seconds: seconds / speed)
        self.progress = progress
        forward = true
        loading = true
        DispatchQueue.main.asyncAfter(deadline: .now() + (seconds + 0.8) / speed) {
            [weak self] in self?.reveal()
        }
    }

    /// A step, as if reached by Continue.
    func jump(to next: WelcomeStep) {
        loading = false
        if next == step { replay += 1 }
        goTo(next)
    }

    /// Checking, then verified: a made-up hash of a made-up disc.
    func simulateCheck() {
        stopCheck()
        let path = "/Users/you/Downloads/Melee.iso"
        disc = .checking(path: path, percent: 0)
        let timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in
            guard let self, case .checking(_, let percent) = disc else { return }
            let next = min(100, percent + max(1, Int(2 * WelcomeMotion.shared.speed)))
            if next >= 100 {
                stopCheck()
                disc = .verified(path: path)
            } else {
                disc = .checking(path: path, percent: next)
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func simulateRefusal() {
        stopCheck()
        disc = .failed(path: "/Users/you/Downloads/Melee PAL.iso",
                       message: "This is the European (PAL) disc. Desktop Melee needs the USA "
                           + "version, 1.02.")
        failures += 1
    }

    func setDisc(_ state: DiscStatus) {
        stopCheck()
        disc = state
    }

    /// A press from a made-up device on the next free port.
    func simulatePress(_ name: String) {
        guard let port = (0..<4).first(where: { p in !pressed.contains { $0.port == p } })
        else { return }
        pressed.append(PortPress(port: port, name: name))
        pressed.sort { $0.port < $1.port }
        MenuSound.move.play()
    }

    // MARK: Window

    private func makeWindow() -> NSWindow {
        if let window { return window }
        // A panel that takes clicks without becoming key, so the game keeps
        // the keyboard in phase 2 (runModal still makes it key in phase 1).
        let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 440, height: 560),
                             styleMask: [.titled, .closable, .fullSizeContentView],
                             backing: .buffered, defer: false)
        window.becomesKeyOnlyIfNeeded = true
        window.hidesOnDeactivate = false
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
        // A fixed size: without this, every animation frame re-measures the
        // window with the window server, which stalls the game loop sharing
        // the main thread.
        let hosting = NSHostingView(rootView: WelcomeView(controller: self))
        hosting.sizingOptions = []
        window.contentView = hosting
        self.window = window
        return window
    }

    func windowWillClose(_ notification: Notification) {
        stopCheck()
        endPhaseTwo()
        if !modal {
            MenuBarController.shared?.focusGame()
        }
        if modal {
            // Closed before a disc was accepted: the game quits.
            result = nil
            modal = false
            NSApp.stopModal()
        }
    }

    // MARK: Dumps (DESKTOP_MELEE_DUMP)

    @MainActor
    private func dumpPhaseOne() {
        guard let dir = ProcessInfo.processInfo.environment["DESKTOP_MELEE_DUMP"] else { return }
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
            sample.firstRun = !again
            sample.oldPath = again ? "/Users/you/Downloads/Melee.iso" : nil
            WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/\(name).png")
        }
    }

    /// DESKTOP_MELEE_DUMP: steps 3+ and the loading screen, once the game is up
    /// (called from desktop_melee_ui_assets_ready on any boot).
    @MainActor
    static func dumpPhaseTwo(to dir: String) {
        let sample = WelcomeController()
        sample.isSample = true
        sample.step = .controls
        sample.pressed = [PortPress(port: 0, name: "GameCube controller"),
                          PortPress(port: 1, name: "DualSense Wireless Controller")]
        WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/welcome_3.png")
        sample.keyboardOpen = true
        WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/welcome_3_keyboard.png")
        sample.keyboardOpen = false
        sample.step = .permission
        WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/welcome_4.png")
        sample.axGranted = true
        WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/welcome_4_granted.png")
        sample.step = .ready
        WelcomeController.dump(WelcomeView(controller: sample), to: "\(dir)/welcome_5.png")
        let loading = WelcomeController()
        loading.isSample = true
        loading.step = .disc
        loading.loading = true
        WelcomeController.dump(WelcomeView(controller: loading), to: "\(dir)/welcome_loading.png")
        // The loading progress: first launch (in the welcome window), and the
        // small window later launches get.
        let early = ShaderProgress.sample(done: 1204, total: 6625, secondsLeft: 250)
        WelcomeController.dump(WelcomeLoading(progress: early).frame(width: 392, height: 470)
            .padding(24).background(Color(white: 0.14)), to: "\(dir)/loading_first.png")
        let late = ShaderProgress.sample(done: 5431, total: 6625, secondsLeft: 40)
        WelcomeController.dump(WelcomeLoading(progress: late).frame(width: 392, height: 470)
            .padding(24).background(Color(white: 0.14)), to: "\(dir)/loading_almost.png")
        let later = ShaderProgress.sample(done: 3102, total: 6625, secondsLeft: 130)
        WelcomeController.dump(LoadingWindowView(progress: later), to: "\(dir)/loading_window.png")
    }

    @MainActor
    static func dump<V: View>(_ view: V, to path: String) {
        let renderer = ImageRenderer(content: view.environment(\.colorScheme, .dark)
            .environment(\.welcomeStill, true))
        renderer.scale = 2
        guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { return }
        try? png.write(to: URL(fileURLWithPath: path))
    }
}
