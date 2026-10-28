// SPDX-License-Identifier: GPL-3.0-or-later
// Loading progress while melee-pc's launcher waits for the graphics
// pipelines (its own screen is never shown in Desktop Melee). The welcome
// window shows it on the first launch; on later launches a small window
// appears only when the wait takes more than two seconds (a new build or a
// macOS update rebuilds them).
import AppKit
import DesktopMeleeBridge
import SwiftUI

final class ShaderProgress: ObservableObject {
    static let shared = ShaderProgress()

    /// The most pipelines seen pending (the queue is full from the start).
    @Published private(set) var total = 0
    @Published private(set) var pending = 0
    /// Seconds left at the current rate, once there is enough to tell.
    @Published private(set) var secondsLeft: Int?

    var fraction: Double { total > 0 ? Double(total - pending) / Double(total) : 0 }
    var active: Bool { total > 0 && pending > 0 }

    private var timer: Timer?
    private var started = Date()
    private var window: NSPanel?
    /// The last second or so of (time, pending), for the current rate.
    private var recent: [(Date, Int)] = []

    /// A fixed sample, for DESKTOP_MELEE_DUMP renders.
    static func sample(done: Int, total: Int, secondsLeft: Int?) -> ShaderProgress {
        let p = ShaderProgress()
        p.total = total
        p.pending = total - done
        p.secondsLeft = secondsLeft
        return p
    }

    /// Debug (the welcome debug panel): a made-up build of `total` pipelines
    /// that takes `seconds`.
    func simulate(seconds: Double, total: Int = 6625) {
        timer?.invalidate()
        self.total = total
        pending = total
        secondsLeft = nil
        let start = Date()
        let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] timer in
            guard let self else { return timer.invalidate() }
            let f = min(1, Date().timeIntervalSince(start) / seconds)
            pending = Int(Double(total) * (1 - f))
            secondsLeft = f > 0.15 ? Int(seconds * (1 - f)) * 20 : nil
            if f >= 1 { timer.invalidate() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func begin() {
        total = 0
        pending = 0
        secondsLeft = nil
        started = Date()
        recent = []
        let timer = Timer(timeInterval: 0.25, repeats: true) { [weak self] _ in self?.tick() }
        // .common: also while the welcome window runs modally.
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func end() {
        timer?.invalidate()
        timer = nil
        pending = 0
        window?.orderOut(nil)
        window = nil
    }

    private func tick() {
        let now = Int(desktop_melee_pipelines_pending())
        total = max(total, now)
        pending = now
        let elapsed = Date().timeIntervalSince(started)
        let done = total - now
        // A rate from the first few hundred is steady enough to estimate.
        if done >= 200, elapsed > 5 {
            secondsLeft = Int(Double(now) / (Double(done) / elapsed))
        }
        // Later launches have no welcome window: a small one, only when the
        // rest will take more than a few seconds at the current rate (a warm
        // cache builds thousands a second and never shows it).
        let stamp = Date()
        recent.append((stamp, now))
        recent.removeAll { stamp.timeIntervalSince($0.0) > 1.5 }
        guard window == nil, now > 0, elapsed > 2, let oldest = recent.first,
              stamp.timeIntervalSince(oldest.0) >= 1,
              !WelcomeController.shared.loading, !WelcomeController.shared.waitingForDisc
        else { return }
        let rate = Double(oldest.1 - now) / stamp.timeIntervalSince(oldest.0)
        if rate <= 0 || Double(now) / rate > 3 {
            showWindow()
        }
    }

    private func showWindow() {
        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 380, height: 150),
                            styleMask: [.titled, .fullSizeContentView, .nonactivatingPanel],
                            backing: .buffered, defer: false)
        panel.titlebarAppearsTransparent = true
        panel.titleVisibility = .hidden
        panel.isMovableByWindowBackground = true
        panel.isReleasedWhenClosed = false
        panel.hidesOnDeactivate = false
        panel.appearance = NSAppearance(named: .darkAqua)
        panel.level = .popUpMenu
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        let hosting = NSHostingView(rootView: LoadingWindowView(progress: self))
        hosting.sizingOptions = [] // see WelcomeController.makeWindow
        panel.contentView = hosting
        panel.center()
        panel.orderFrontRegardless()
        window = panel
        NSLog("desktop-melee: loading window shown (%d pipelines left)", pending)
    }

    /// "about 4 min left", "less than a minute left", or "" while unknown.
    static func timeLeft(_ seconds: Int?) -> String {
        guard let seconds else { return "" }
        if seconds < 60 { return "less than a minute left" }
        return "about \(Int((Double(seconds) / 60).rounded())) min left"
    }
}

/// The bar and its two labels, shared by the welcome window and the small one.
struct ShaderProgressBar: View {
    @ObservedObject var progress: ShaderProgress

    var body: some View {
        VStack(spacing: 6) {
            ProgressBar(fraction: progress.fraction)
            HStack {
                MText("Graphics: \(Self.count(progress.total - progress.pending)) of \(Self.count(progress.total))", size: 14)
                Spacer(minLength: 8)
                MText(ShaderProgress.timeLeft(progress.secondsLeft), size: 14)
            }
            .foregroundStyle(.secondary)
        }
    }
}

extension ShaderProgressBar {
    /// 6625 → "6,625".
    static func count(_ n: Int) -> String {
        n.formatted(.number.grouping(.automatic))
    }
}

/// Later launches: the small window.
struct LoadingWindowView: View {
    @ObservedObject var progress: ShaderProgress

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .frame(width: 26, height: 26)
                VStack(alignment: .leading, spacing: 1) {
                    MText("Getting Melee ready...", size: 19)
                    MParagraph("Rebuilding graphics after an update. Only this once.", size: 14, width: 300)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
            }
            Spacer(minLength: 0)
            ShaderProgressBar(progress: progress)
        }
        .padding(.horizontal, 20)
        .padding(.top, 26)
        .padding(.bottom, 18)
        .frame(width: 380, height: 150)
        .background(Color(white: 0.14))
    }
}

@_cdecl("desktop_melee_ui_loading_begin")
public func desktopMeleeUILoadingBegin() {
    ShaderProgress.shared.begin()
}

@_cdecl("desktop_melee_ui_loading_end")
public func desktopMeleeUILoadingEnd() {
    ShaderProgress.shared.end()
}
