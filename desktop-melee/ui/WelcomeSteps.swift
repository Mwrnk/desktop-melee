// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window's steps, in the popover's dark section style and Melee's
// font. A release build has no font of its own: step 1 (the disc) is in the
// system font, and the reveal after "Getting Melee ready" crossfades to Melee's
// font and emblem. Motion comes from WelcomeMotion.swift.
import AppKit
import DesktopMeleeBridge
import SwiftUI

struct WelcomeView: View {
    @ObservedObject var controller: WelcomeController
    @ObservedObject private var assets = GameAssets.shared
    @ObservedObject private var motion = WelcomeMotion.shared

    /// Changes whenever another screen shows: it slides in.
    private var screenKey: Int {
        controller.loading ? 0 : controller.step.rawValue * 1000 + controller.replay
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            ZStack(alignment: .topLeading) {
                // Pinned to the content width: something too wide overflows on
                // the right instead of pushing the whole step sideways.
                screen
                    .frame(width: 392, alignment: .topLeading)
                    .frame(maxHeight: .infinity, alignment: .top)
                    .id(screenKey)
                    .transition(.welcomeStep(forward: controller.forward, motion: motion))
            }
            .animation(motion.step, value: screenKey)
        }
        .padding(.horizontal, 24)
        .padding(.top, 34)
        // Clear of the window's rounded bottom corners.
        .padding(.bottom, 32)
        // Pinned to the top: a tall step never pushes the header down.
        .frame(width: 440, height: 570, alignment: .top)
        .background(Color(white: 0.14))
        .foregroundStyle(.primary)
        // The reveal: Melee's font and emblem arrive with the disc.
        .animation(motion.step, value: assets.shown)
    }

    @ViewBuilder
    private var screen: some View {
        if controller.loading {
            WelcomeLoading(progress: controller.progress)
        } else {
            switch controller.step {
            case .welcome: WelcomeIntroStep(controller: controller)
            case .disc: WelcomeDiscStep(controller: controller)
            case .controls: WelcomeControlsStep(controller: controller)
            case .permission: WelcomePermissionStep(controller: controller)
            case .ready: WelcomeReadyStep(controller: controller)
            }
        }
    }

    private var header: some View {
        HStack(spacing: 10) {
            // The app's icon, until the game has decoded the Smash emblem from
            // the disc; then the emblem grows in its place.
            ZStack {
                if assets.shown, let emblem = GameImages.template("emblem", height: 28) {
                    Image(nsImage: emblem)
                        .renderingMode(.template)
                        .transition(.pop(0.4))
                } else {
                    Image(nsImage: NSApp.applicationIconImage)
                        .resizable()
                        .transition(.fade)
                }
            }
            .frame(width: 30, height: 30)
            MText("Desktop Melee", size: 24)
            Spacer()
            StepDots(current: controller.step.rawValue, count: WelcomeStep.allCases.count)
        }
        .padding(.bottom, 18)
    }
}

/// Where you are: one dot per step, the current one stretched into a pill.
struct StepDots: View {
    let current: Int
    let count: Int

    var body: some View {
        HStack(spacing: 5) {
            ForEach(1...count, id: \.self) { i in
                Capsule()
                    .fill(i == current ? PlayerColor.p1
                          : Color.white.opacity(i < current ? 0.55 : 0.18))
                    .frame(width: i == current ? 18 : 6, height: 6)
            }
        }
        .animation(WelcomeMotion.shared.pop, value: current)
        .accessibilityElement()
        .accessibilityLabel("Step \(current) of \(count)")
    }
}

/// A big full-width button in P1 red, like the Match tab's Start.
struct WelcomeButton: View {
    let title: String
    var enabled = true
    let action: () -> Void

    init(_ title: String, enabled: Bool = true, action: @escaping () -> Void) {
        self.title = title
        self.enabled = enabled
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            // Fits beside Back too.
            MText(title, size: fittedSize(title, size: 24, width: 236))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 11)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(enabled ? PlayerColor.p1 : Color.primary.opacity(0.12))
                )
                .foregroundStyle(enabled ? Color.white : Color.secondary)
                .contentShape(Rectangle())
        }
        .buttonStyle(PressStyle())
        .disabled(!enabled)
        .keyboardShortcut(.defaultAction)
        // Unlocking (a verified disc) fades it to red.
        .animation(WelcomeMotion.shared.quick, value: enabled)
    }
}

/// The bottom row: Back where there is somewhere to go back to, then the
/// step's main button.
struct WelcomeNav: View {
    @ObservedObject var controller: WelcomeController
    let title: String
    var enabled = true
    let action: () -> Void

    var body: some View {
        HStack(spacing: 8) {
            if controller.canGoBack {
                Button { controller.goBack() } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 13, weight: .bold))
                        MText("Back", size: 22)
                    }
                    .padding(.horizontal, 16)
                    .frame(maxHeight: .infinity)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(Color.primary.opacity(0.1))
                    )
                    .contentShape(Rectangle())
                }
                .buttonStyle(PressStyle())
            }
            WelcomeButton(title, enabled: enabled, action: action)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
}

/// Section titles: small capitals, like the popover's.
struct WelcomeLabel: View {
    let text: String

    init(_ text: String) { self.text = text }

    var body: some View {
        MText(text.uppercased(), size: 15, tracking: 0.5)
            .foregroundStyle(.secondary)
    }
}

/// A rounded group, like the popover's sections.
struct WelcomeBox<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) { content }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color.primary.opacity(0.06))
            )
    }
}

// MARK: - 1. Welcome

struct WelcomeIntroStep: View {
    @ObservedObject var controller: WelcomeController

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            DesktopSketch()
                .frame(height: 200)
                .entrance(0)
            MText("Desktop Melee", size: 42)
                .entrance(1)
            MParagraph("Real Melee, played right on your desktop. Your Dock is the stage, and "
                       + "the edges of the screen are the blast zones.", size: 21, width: 392)
                .foregroundStyle(.secondary)
                .entrance(2)
            Spacer(minLength: 0)
            WelcomeNav(controller: controller, title: "Get started") { controller.goTo(.controls) }
                .entrance(3)
        }
    }
}

/// A desktop drawn with shapes (no Nintendo art): menu bar, two windows, the
/// Dock as the stage with P1 and P2 hopping on it, and the screen edge dashed
/// as the blast zone.
/// Every loop in the welcome window is a `Looping` (WelcomeMotion.swift),
/// around just the part that moves.
struct DesktopSketch: View {
    var body: some View {
        GeometryReader { g in
            let w = g.size.width, h = g.size.height
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color(white: 0.09))
                Rectangle()
                    .fill(Color(white: 0.2))
                    .frame(width: w, height: 14)
                    .clipShape(UnevenRoundedRectangle(topLeadingRadius: 12,
                                                      topTrailingRadius: 12))
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(white: 0.22))
                    .frame(width: w * 0.42, height: h * 0.42)
                    .offset(x: w * 0.12, y: h * 0.22)
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(white: 0.18))
                    .frame(width: w * 0.32, height: h * 0.34)
                    .offset(x: w * 0.56, y: h * 0.14)
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(white: 0.32))
                    .frame(width: w * 0.5, height: 16)
                    .offset(x: w * 0.25, y: h - 26)
                fighter(PlayerColor.p1, period: 1.5, phase: 0)
                    .offset(x: w * 0.36, y: h - 43)
                fighter(PlayerColor.p2, period: 1.8, phase: 0.4)
                    .offset(x: w * 0.6, y: h - 43)
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .strokeBorder(PlayerColor.p1.opacity(0.8),
                                  style: StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
            }
        }
        .accessibilityElement()
        .accessibilityLabel("Your desktop: the Dock is the stage, the screen edges are the "
                            + "blast zones")
    }

    private func fighter(_ color: Color, period: Double, phase: Double) -> some View {
        Looping(.hop(height: 16, period: period, phase: phase)) {
            Circle().fill(color)
                .frame(width: 16, height: 16)
        }
    }
}

// MARK: - 2. Your disc

struct WelcomeDiscStep: View {
    @ObservedObject var controller: WelcomeController
    @State private var targeted = false

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            MText(title, size: fittedSize(title, size: 31, width: 392))
            MParagraph(controller.findAgain
                       ? "Desktop Melee can't use the disc it played last time. Choose your "
                           + "Melee disc image again."
                       : "Desktop Melee includes the game's code but none of Nintendo's data. "
                           + "Characters, stages and sound come from your own copy of Melee.",
                       size: 19, width: 392)
                .foregroundStyle(.secondary)
            if controller.findAgain, let old = controller.oldPath {
                Label(old, systemImage: "clock.arrow.circlepath")
                    .font(.system(size: 12))
                    .foregroundStyle(.tertiary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            dropZone
            ZStack(alignment: .topLeading) {
                status
                    .id(statusKey)
                    .transition(.rise(8))
            }
            .animation(WelcomeMotion.shared.step, value: statusKey)
            Spacer(minLength: 0)
            WelcomeNav(controller: controller, title: "Continue", enabled: isVerified) {
                controller.acceptDisc()
            }
        }
    }

    private var title: String {
        controller.findAgain ? "Find your Melee disc again" : "Bring your own Melee"
    }

    private var isVerified: Bool {
        if case .verified = controller.disc { return true }
        return false
    }

    private var isFailed: Bool {
        if case .failed = controller.disc { return true }
        return false
    }

    /// Changes when the status row shows something else (not with progress);
    /// each refusal is new, so it shakes again.
    private var statusKey: Int {
        switch controller.disc {
        case .empty: return 0
        case .checking: return 1
        case .verified: return 2
        case .failed: return 3 + controller.failures * 10
        }
    }

    private var dropZone: some View {
        let motion = WelcomeMotion.shared
        return VStack(spacing: 5) {
            Image(systemName: "opticaldisc")
                .font(.system(size: 28))
                .foregroundStyle(targeted ? PlayerColor.p1 : .secondary)
                .offset(y: targeted && !motion.reduce ? -3 : 0)
            MText(targeted ? "Drop it" : "Drop your disc image here", size: 21)
            MText("Melee USA 1.02:  .iso  .gcm  .ciso  .rvz", size: 15)
                .foregroundStyle(.secondary)
            Button { controller.chooseFile() } label: {
                MText(isFailed ? "Choose another..." : "Choose file...", size: 19)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 6)
                    .background(Capsule().fill(Color.primary.opacity(0.14)))
                    .contentShape(Capsule())
            }
            .buttonStyle(PressStyle())
            .padding(.top, 4)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 10)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(targeted ? PlayerColor.p1.opacity(0.1) : Color.primary.opacity(0.04))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(targeted ? PlayerColor.p1 : Color.primary.opacity(0.25),
                              style: StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
        )
        .scaleEffect(targeted && !motion.reduce ? 1.02 : 1)
        .animation(motion.quick, value: targeted)
        .modifier(DropTarget(targeted: $targeted, live: !controller.isSample) {
            controller.drop($0)
        })
    }

    @ViewBuilder
    private var status: some View {
        switch controller.disc {
        case .empty:
            Color.clear.frame(height: 0)
        case .checking(let path, let percent):
            WelcomeBox {
                row(title: "Checking your disc… \(percent)%", detail: fileName(path)) {
                    Spinning(period: 1.2) {
                        Image(systemName: "opticaldisc").foregroundStyle(.secondary)
                    }
                }
                ProgressBar(fraction: Double(percent) / 100)
            }
        case .verified(let path):
            WelcomeBox {
                row(title: "Super Smash Bros. Melee · USA · 1.02 · verified",
                    detail: fileName(path)) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                        .modifier(PopOnAppear(from: 0.3))
                }
            }
        case .failed(let path, let message):
            WelcomeBox {
                row(title: message, detail: fileName(path)) {
                    Image(systemName: "xmark.octagon.fill").foregroundStyle(.red)
                }
            }
            .modifier(ShakeOnAppear())
        }
    }

    private func fileName(_ path: String) -> String { (path as NSString).lastPathComponent }

    private func row<Icon: View>(title: String, detail: String,
                                 @ViewBuilder icon: () -> Icon) -> some View {
        HStack(alignment: .top, spacing: 10) {
            icon()
                .font(.system(size: 15))
                .frame(width: 18, height: 18)
            VStack(alignment: .leading, spacing: 2) {
                MParagraph(title, size: 18, width: 330)
                MText(detail, size: 15)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: 330, alignment: .leading)
                    .clipped()
            }
        }
    }
}

/// A thin progress bar drawn with shapes (ImageRenderer can't draw ProgressView).
struct ProgressBar: View {
    let fraction: Double

    var body: some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                Capsule().fill(Color.primary.opacity(0.12))
                Capsule().fill(Color.green)
                    .frame(width: max(4, g.size.width * min(max(fraction, 0), 1)))
            }
        }
        .frame(height: 5)
    }
}

/// Accepts dropped files; left off in offscreen renders, which can't draw a
/// drop target.
struct DropTarget: ViewModifier {
    @Binding var targeted: Bool
    let live: Bool
    let perform: ([NSItemProvider]) -> Bool

    func body(content: Content) -> some View {
        if live {
            content.onDrop(of: [.fileURL], isTargeted: $targeted, perform: perform)
        } else {
            content
        }
    }
}

// MARK: - Getting Melee ready

/// Between the disc and step 3: the graphics are built (the first launch
/// takes minutes, so it shows progress), then the game boots and decodes its
/// assets.
struct WelcomeLoading: View {
    @ObservedObject var progress: ShaderProgress = .shared

    var body: some View {
        VStack(spacing: 14) {
            Spacer()
            Spinning(period: 2.4) {
                Image(systemName: "opticaldisc")
                    .font(.system(size: 44))
                    .foregroundStyle(.secondary)
            }
            .entrance(0)
            MText("Getting Melee ready...", size: 26)
                .entrance(1)
            if progress.active {
                VStack(spacing: 14) {
                    MParagraph("The first launch builds Melee's graphics, so matches never "
                               + "stutter. It takes a few minutes, once. You can use your Mac "
                               + "in the meantime.", size: 17, width: 340, alignment: .center)
                        .foregroundStyle(.secondary)
                    ShaderProgressBar(progress: progress)
                        .frame(width: 340)
                        .padding(.top, 6)
                }
                .entrance(2)
            }
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - 3. Controls

struct WelcomeControlsStep: View {
    @ObservedObject var controller: WelcomeController

    static let playerColors = [PlayerColor.p1, PlayerColor.p2,
                               Color(red: 0.95, green: 0.75, blue: 0.15),
                               Color(red: 0.25, green: 0.70, blue: 0.35)]

    var body: some View {
        let motion = WelcomeMotion.shared
        VStack(alignment: .leading, spacing: 12) {
            MText("Grab a controller", size: 31)
            HStack(spacing: 8) {
                DeviceCard(lit: litColor(.gameCube)) {
                    DeviceIcon(kind: .gameCube)
                } text: {
                    cardText("GameCube", "Official adapter, or one in Wii U mode")
                }
                DeviceCard(lit: litColor(.otherPad)) {
                    DeviceIcon(kind: .otherPad)
                } text: {
                    cardText("Xbox, PS, Switch", "USB or Bluetooth")
                }
                KeyboardCard(open: controller.keyboardOpen, lit: litColor(.keyboard)) {
                    controller.keyboardOpen.toggle()
                }
            }
            ZStack(alignment: .topLeading) {
                if controller.keyboardOpen {
                    keyboardKeys.transition(.rise(-8))
                } else {
                    players.transition(.rise(8))
                }
            }
            .animation(motion.step, value: controller.keyboardOpen)
            Spacer(minLength: 0)
            WelcomeNav(controller: controller, title: "Continue") {
                controller.goTo(.permission)
            }
        }
    }

    static func color(_ port: Int) -> Color { playerColors[min(port, 3)] }

    /// The color of the first player whose press matches this kind of device.
    private func litColor(_ kind: WelcomeController.DeviceKind) -> Color? {
        controller.pressed.first { WelcomeController.kind(of: $0.name) == kind }
            .map { Self.color($0.port) }
    }

    private func cardText(_ title: String, _ detail: String) -> some View {
        VStack(spacing: 5) {
            MText(title, size: fittedSize(title, size: 17, width: 112))
            MParagraph(detail, size: 14, width: 112, alignment: .center)
                .foregroundStyle(.secondary)
        }
    }

    // MARK: Players

    /// Four slots that fill as players press: the screen is listening.
    private var players: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                ListeningDot()
                WelcomeLabel(controller.pressed.isEmpty ? "Press any button"
                             : "Listening for more players")
            }
            let columns = [GridItem(.flexible(), spacing: 8), GridItem(.flexible())]
            LazyVGrid(columns: columns, spacing: 8) {
                ForEach(0..<4, id: \.self) { port in
                    PlayerSlot(port: port,
                               press: controller.pressed.first { $0.port == port },
                               next: port == firstFree)
                }
            }
            .animation(WelcomeMotion.shared.pop, value: controller.pressed)
            MParagraph("Press a button on each controller, or a key, to see who's who. Ports "
                       + "can be changed in the menu bar's Controls tab.", size: 15, width: 392)
                .foregroundStyle(.secondary)
        }
    }

    private var firstFree: Int? {
        (0..<4).first { p in !controller.pressed.contains { $0.port == p } }
    }

    // MARK: Keyboard

    private func key(_ slot: Int32) -> String { String(cString: desktop_melee_key_name(slot)) }

    private var keyboardKeys: some View {
        WelcomeBox {
            let buttons: [(badge: Int, slot: Int32)] = [(0, 0), (1, 1), (2, 2), (3, 3),
                                                        (4, 5), (5, 6), (6, 7)]
            let columns = [GridItem(.flexible(), alignment: .leading),
                           GridItem(.flexible(), alignment: .leading)]
            LazyVGrid(columns: columns, alignment: .leading, spacing: 4) {
                ForEach(buttons, id: \.badge) { b in
                    HStack(spacing: 8) {
                        PadBadge(button: b.badge)
                        KeyCap(key(b.slot))
                    }
                }
                HStack(spacing: 8) {
                    MText("Start", size: 16).foregroundStyle(.secondary)
                    KeyCap(key(4))
                }
            }
            keyRow("Stick") { stickKeys }
            keyRow("C-stick") {
                ForEach([18, 17, 19, 16], id: \.self) { KeyCap(key(Int32($0))) }
            }
            MParagraph("The keyboard works while Desktop Melee is the active app.",
                       size: 15, width: 360)
                .foregroundStyle(.secondary)
        }
    }

    /// The stick keys, read live from the bindings (up left down right).
    @ViewBuilder
    private var stickKeys: some View {
        let keys = [14, 13, 15, 12].map { key(Int32($0)) }
        if keys == ["Up", "Left", "Down", "Right"] {
            KeyCap("Arrows")
            MText("or", size: 15).foregroundStyle(.secondary)
            ForEach(["W", "A", "S", "D"], id: \.self) { KeyCap($0) }
        } else {
            ForEach(keys.indices, id: \.self) { KeyCap(keys[$0]) }
        }
    }

    private func keyRow<C: View>(_ label: String, @ViewBuilder keys: () -> C) -> some View {
        HStack(spacing: 4) {
            MText(label, size: 16).foregroundStyle(.secondary)
                .frame(width: 66, alignment: .leading)
            keys()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// One kind of device. Lights up in a player's color when that player's press
/// came from one: the border, a tint and a hop of the icon.
struct DeviceCard<Icon: View, Caption: View>: View {
    let lit: Color?
    @ViewBuilder let icon: Icon
    @ViewBuilder let text: Caption

    var body: some View {
        let motion = WelcomeMotion.shared
        VStack(spacing: 5) {
            icon
                .foregroundStyle(lit ?? Color.secondary)
                .frame(height: 26)
                .phaseAnimator([1.0, 1.25, 1.0], trigger: lit != nil) { view, scale in
                    view.scaleEffect(motion.reduce || lit == nil ? 1 : scale)
                } animation: { _ in motion.easeOut(0.16) }
            text
        }
        .frame(maxWidth: .infinity, minHeight: 100, alignment: .top)
        .padding(.vertical, 8)
        .padding(.horizontal, 6)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(lit.map { $0.opacity(0.12) } ?? Color.primary.opacity(0.06))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(lit ?? .clear, lineWidth: 2)
        )
        .animation(motion.pop, value: lit)
        .contentShape(Rectangle())
    }
}

/// The keyboard card is a button (the key layout), and looks like one: a
/// "Show keys" pill with a chevron, a hover highlight and a press.
struct KeyboardCard: View {
    let open: Bool
    let lit: Color?
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        let motion = WelcomeMotion.shared
        Button(action: action) {
            DeviceCard(lit: lit) {
                DeviceIcon(kind: .keyboard)
            } text: {
                VStack(spacing: 6) {
                    MText("Keyboard", size: 17)
                    HStack(spacing: 4) {
                        MText(open ? "Hide keys" : "Show keys", size: 15)
                        Image(systemName: "chevron.down")
                            .font(.system(size: 9, weight: .bold))
                            .rotationEffect(.degrees(open ? 180 : 0))
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 4)
                    .background(Capsule().fill(Color.primary.opacity(hovering || open ? 0.22
                                                                         : 0.14)))
                }
            }
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color.primary.opacity(hovering ? 0.05 : 0))
                    .allowsHitTesting(false)
            )
        }
        .buttonStyle(PressStyle())
        .onHover { hovering = $0 }
        .animation(motion.quick, value: hovering)
        .animation(motion.step, value: open)
        .accessibilityLabel(open ? "Hide the keyboard keys" : "Show the keyboard keys")
    }
}

/// One player port: dashed and waiting, or filled in the player's color with
/// the device that pressed. The next slot to fill breathes.
struct PlayerSlot: View {
    let port: Int
    let press: WelcomeController.PortPress?
    let next: Bool

    var body: some View {
        let motion = WelcomeMotion.shared
        let color = WelcomeControlsStep.color(port)
        HStack(spacing: 8) {
            MText("P\(port + 1)", size: 22)
                .foregroundStyle(press == nil ? color.opacity(0.45) : color)
            if let press {
                let name = Self.shortName(press.name)
                MText(name, size: fittedSize(name, size: 18, width: 104))
                    .transition(.rise(4))
                    .help(press.name)
                Spacer(minLength: 0)
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                    .transition(.pop(0.3))
            } else {
                MText("Waiting", size: 16)
                    .foregroundStyle(.tertiary)
                    .transition(.fade)
                Spacer(minLength: 0)
            }
        }
        .padding(.horizontal, 10)
        .frame(height: 40)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(press == nil ? Color.primary.opacity(0.04) : color.opacity(0.16))
        )
        .overlay { border(color) }
        .phaseAnimator([1.0, 1.05, 1.0], trigger: press != nil) { view, scale in
            view.scaleEffect(motion.reduce || press == nil ? 1 : scale)
        } animation: { _ in motion.easeOut(0.14) }
        .animation(motion.pop, value: press)
    }

    @ViewBuilder
    private func border(_ color: Color) -> some View {
        let shape = RoundedRectangle(cornerRadius: 10, style: .continuous)
        if press != nil {
            shape.strokeBorder(color.opacity(0.9), lineWidth: 1.5)
        } else if next {
            Looping(.breathe(low: 0.25, period: 1.6)) {
                shape.strokeBorder(color.opacity(0.7),
                                   style: StrokeStyle(lineWidth: 1.5, dash: [5, 4]))
            }
        } else {
            shape.strokeBorder(Color.primary.opacity(0.14),
                               style: StrokeStyle(lineWidth: 1.5, dash: [5, 4]))
        }
    }

    /// "DualSense Wireless Controller" → "DualSense": the slot is small, the
    /// full name is in the tooltip.
    static func shortName(_ name: String) -> String {
        for suffix in [" Wireless Controller", " Controller", " controller"]
        where name.hasSuffix(suffix) {
            return String(name.dropLast(suffix.count))
        }
        return name
    }
}

/// A red dot with a ring that keeps spreading from it: listening.
struct ListeningDot: View {
    var body: some View {
        ZStack {
            Looping(.ripple(scale: 2.6, period: 1.4)) {
                Circle().stroke(PlayerColor.p1, lineWidth: 1.2)
            }
            Circle().fill(PlayerColor.p1)
        }
        .frame(width: 7, height: 7)
        .frame(width: 14, height: 14)
        .accessibilityHidden(true)
    }
}

/// A key on a keyboard, with its name.
struct KeyCap: View {
    let name: String

    init(_ name: String) { self.name = name }

    var body: some View {
        MText(name, size: 16)
            .padding(.horizontal, 6)
            .frame(minWidth: 22, minHeight: 21)
            .background(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(Color.primary.opacity(0.1))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .strokeBorder(Color.primary.opacity(0.18), lineWidth: 1)
                    .padding(.bottom, -0.5)
            )
            .overlay(alignment: .bottom) {
                // The key's front edge.
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(Color.black.opacity(0.35))
                    .frame(height: 2)
                    .padding(.horizontal, 2)
                    .offset(y: 1)
            }
    }
}

/// The three device kinds, drawn (no Nintendo art): a GameCube pad (filled,
/// with its big A and C-stick), another pad (outlined, two sticks and four
/// face buttons) and a keyboard.
struct DeviceIcon: View {
    let kind: WelcomeController.DeviceKind

    var body: some View {
        Canvas { ctx, size in
            let u = size.width / 30 // drawn on a 30 x 22 grid
            func r(_ x: Double, _ y: Double, _ w: Double, _ h: Double) -> CGRect {
                CGRect(x: x * u, y: y * u, width: w * u, height: h * u)
            }
            func circle(_ x: Double, _ y: Double, _ radius: Double) -> Path {
                Path(ellipseIn: r(x - radius, y - radius, radius * 2, radius * 2))
            }
            let shading = GraphicsContext.Shading.foreground
            switch kind {
            case .gameCube, .otherPad:
                var body = Path(roundedRect: r(3, 2, 24, 11), cornerRadius: 5.5 * u)
                body.addPath(Path(roundedRect: r(2.5, 6, 9, 15), cornerRadius: 4.5 * u))
                body.addPath(Path(roundedRect: r(18.5, 6, 9, 15), cornerRadius: 4.5 * u))
                if kind == .gameCube {
                    ctx.fill(body, with: shading)
                    ctx.blendMode = .destinationOut
                    ctx.stroke(circle(8.5, 8, 2.6), with: .color(.black), lineWidth: 1.4 * u)
                    ctx.fill(circle(21.5, 8, 2.6), with: .color(.black))
                    ctx.fill(circle(18, 10.8, 1.2), with: .color(.black))
                    ctx.fill(circle(18.5, 15.5, 1.6), with: .color(.black))
                    ctx.fill(Path(r(10.4, 15, 3.2, 1.1)), with: .color(.black))
                    ctx.fill(Path(r(11.45, 13.95, 1.1, 3.2)), with: .color(.black))
                } else {
                    ctx.stroke(body, with: shading, lineWidth: 1.5 * u)
                    ctx.stroke(circle(9.5, 13, 2.2), with: shading, lineWidth: 1.3 * u)
                    ctx.stroke(circle(18.5, 15.5, 1.8), with: shading, lineWidth: 1.3 * u)
                    for (x, y) in [(21.5, 6.2), (19.3, 8.4), (23.7, 8.4), (21.5, 10.6)] {
                        ctx.fill(circle(x, y, 1.05), with: shading)
                    }
                    ctx.fill(Path(r(5.2, 7.9, 4.6, 1.2)), with: shading)
                    ctx.fill(Path(r(6.9, 6.2, 1.2, 4.6)), with: shading)
                }
            case .keyboard:
                let frame = Path(roundedRect: r(1.5, 3.5, 27, 15), cornerRadius: 3 * u)
                ctx.stroke(frame, with: shading, lineWidth: 1.5 * u)
                for row in 0..<2 {
                    for col in 0..<6 {
                        let x = 4.5 + Double(col) * 3.75, y = 6.8 + Double(row) * 3.4
                        ctx.fill(Path(roundedRect: r(x, y, 2.3, 2), cornerRadius: 0.5 * u),
                                 with: shading)
                    }
                }
                ctx.fill(Path(roundedRect: r(9, 14, 12, 2), cornerRadius: 0.8 * u),
                         with: shading)
            }
        }
        .frame(width: 30, height: 22)
        .accessibilityHidden(true)
    }
}

// MARK: - 4. Permission

struct WelcomePermissionStep: View {
    @ObservedObject var controller: WelcomeController
    private static let title = "Let Desktop Melee measure your Dock"

    var body: some View {
        let motion = WelcomeMotion.shared
        let granted = controller.axGranted
        VStack(alignment: .leading, spacing: 16) {
            MText(Self.title, size: fittedSize(Self.title, size: 31, width: 392))
            MParagraph("Accessibility lets the stage match your Dock to the pixel. Desktop "
                       + "Melee only reads where the Dock is.", size: 19, width: 392)
                .foregroundStyle(.secondary)
            DockMeasureSketch(exact: granted)
                .frame(height: 104)
            HStack(spacing: 10) {
                ZStack {
                    if granted {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                            .transition(.pop(0.3))
                    } else {
                        Spinning(period: 4) {
                            Image(systemName: "circle.dashed")
                                .foregroundStyle(.orange)
                        }
                        .transition(.fade)
                    }
                }
                .frame(width: 18, height: 18)
                ZStack(alignment: .leading) {
                    let status = granted ? "Turned on: measured exactly"
                        : controller.axRequested ? "Turn on Desktop Melee in the list"
                        : "Not turned on yet"
                    MText(status, size: fittedSize(status, size: 18, width: 350))
                        .id(status)
                        .transition(.fade)
                }
            }
            .padding(.horizontal, 4)
            .animation(motion.pop, value: granted)
            // macOS ties the permission to each build's signature, so after an
            // update an old entry can show as on and still not count.
            if controller.axRequested && !granted {
                MParagraph("Already on? Select Desktop Melee, remove it with the minus "
                           + "button, then add it again with plus. macOS forgets it after "
                           + "each update.", size: 16, width: 392)
                    .foregroundStyle(.secondary)
                    .transition(.fade)
            }
            Spacer(minLength: 0)
            // Once it is on, the step is done: on to the last one.
            WelcomeNav(controller: controller,
                       title: granted ? "Continue" : "Open System Settings") {
                if granted {
                    controller.goTo(.ready)
                } else {
                    controller.requestAccessibility()
                }
            }
            if !granted {
                // A real button, like Back: skipping is a fine answer.
                Button { controller.goTo(.ready) } label: {
                    MText("Not now: estimate the Dock size", size: 19)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 10)
                        .background(
                            RoundedRectangle(cornerRadius: 10, style: .continuous)
                                .fill(Color.primary.opacity(0.1))
                        )
                        .contentShape(Rectangle())
                }
                .buttonStyle(PressStyle())
            }
        }
    }
}

/// The Dock with a measuring bracket over it: loose and orange while the size
/// is estimated, snapping to the Dock's edges in green once it is measured.
struct DockMeasureSketch: View {
    let exact: Bool

    var body: some View {
        GeometryReader { g in
            let w = g.size.width, h = g.size.height
            let dock = w * 0.56
            let color: Color = exact ? .green : .orange
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color(white: 0.09))
                // The Dock and its icons.
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .fill(Color(white: 0.3))
                    .frame(width: dock, height: 26)
                    .offset(x: (w - dock) / 2, y: h - 36)
                ForEach(0..<6, id: \.self) { i in
                    RoundedRectangle(cornerRadius: 4, style: .continuous)
                        .fill(Color(white: 0.45))
                        .frame(width: 16, height: 16)
                        .offset(x: (w - dock) / 2 + 12 + CGFloat(i) * (dock - 40) / 5,
                                y: h - 31)
                }
                // The bracket over it, Dock-wide, stretched while loose (a
                // scale, so it is never redrawn).
                Group {
                    if exact {
                        bracket(span: dock, color: color)
                    } else {
                        Looping(.stretch(from: 1.06, to: 1.16, period: 2.2)) {
                            bracket(span: dock, color: color)
                        }
                    }
                }
                .offset(x: (w - dock) / 2, y: h - 66)
                MText(exact ? "Exact" : "Estimated", size: 15)
                    .foregroundStyle(color)
                    .frame(width: w)
                    .offset(y: h - 92)
            }
            .animation(WelcomeMotion.shared.pop, value: exact)
        }
        .accessibilityHidden(true)
    }

    private func bracket(span: CGFloat, color: Color) -> some View {
        Path { p in
            p.move(to: CGPoint(x: 0, y: 12))
            p.addLine(to: CGPoint(x: 0, y: 2))
            p.addLine(to: CGPoint(x: span, y: 2))
            p.addLine(to: CGPoint(x: span, y: 12))
        }
        .stroke(color, style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round,
                                          dash: exact ? [] : [5, 4]))
        .frame(width: span, height: 14)
    }
}

// MARK: - 5. Ready

struct WelcomeReadyStep: View {
    @ObservedObject var controller: WelcomeController

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            MenuBarSketch()
                .frame(height: 120)
                .entrance(0)
            MText("You're set", size: 42)
                .entrance(1)
            MParagraph("Desktop Melee lives in your menu bar. Use it to pick characters, change "
                       + "controls and start a match.", size: 21, width: 392)
                .foregroundStyle(.secondary)
                .entrance(2)
            Spacer(minLength: 0)
            WelcomeNav(controller: controller, title: "Start a match") {
                controller.startMatch()
            }
            if StartButton.enabled {
                MText("or press Start on your controller", size: 16)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
            }
        }
    }
}

/// The top of a screen: the menu bar, with an arrow bobbing at the Smash icon.
struct MenuBarSketch: View {
    @ObservedObject private var assets = GameAssets.shared

    var body: some View {
        GeometryReader { g in
            let w = g.size.width
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color(white: 0.09))
                Rectangle()
                    .fill(Color(white: 0.2))
                    .frame(width: w, height: 30)
                    .clipShape(UnevenRoundedRectangle(topLeadingRadius: 12,
                                                      topTrailingRadius: 12))
                // Other menu bar items, then the Smash icon.
                ForEach(0..<3) { i in
                    Capsule().fill(Color(white: 0.35))
                        .frame(width: 18, height: 8)
                        .offset(x: w - 40 - CGFloat(i) * 28, y: 11)
                }
                // A soft highlight behind the icon (a gradient, not a blur),
                // breathing with the arrow.
                Looping(.breathe(low: 0.4, period: 1.3)) {
                    Circle()
                        .fill(RadialGradient(colors: [PlayerColor.p1.opacity(0.5),
                                                      PlayerColor.p1.opacity(0)],
                                             center: .center, startRadius: 0, endRadius: 18))
                        .frame(width: 36, height: 36)
                }
                .offset(x: w - 157, y: -3)
                emblem
                    .foregroundStyle(.white)
                    .frame(width: 22, height: 22)
                    .offset(x: w - 150, y: 4)
                Looping(.bob(distance: 4, period: 1.3)) {
                    Image(systemName: "arrow.up")
                        .font(.system(size: 30, weight: .bold))
                        .foregroundStyle(PlayerColor.p1)
                }
                .offset(x: w - 154, y: 40)
                MText("Smash icon", size: 18)
                    .foregroundStyle(.secondary)
                    .offset(x: w - 262, y: 74)
            }
        }
    }

    @ViewBuilder
    private var emblem: some View {
        if assets.shown, let emblem = GameImages.template("emblem", height: 18) {
            Image(nsImage: emblem).renderingMode(.template)
        } else {
            Image(nsImage: SmashEmblem.image(size: 18)).renderingMode(.template)
        }
    }
}
