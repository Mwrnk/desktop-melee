// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window's steps, in the popover's dark section style and Melee's
// font (bundled with the app, so it is there before the disc). The emblem and
// sounds still come from the disc.
import AppKit
import PetBridge
import SwiftUI

struct WelcomeView: View {
    @ObservedObject var controller: WelcomeController
    @ObservedObject private var assets = GameAssets.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            Group {
                if controller.loading {
                    WelcomeLoading()
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
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        }
        .padding(.horizontal, 24)
        .padding(.top, 34)
        .padding(.bottom, 22)
        .frame(width: 440, height: 560)
        .background(Color(white: 0.14))
        .foregroundStyle(.primary)
    }

    private var header: some View {
        HStack(spacing: 10) {
            // The Smash emblem once the game has decoded it from the disc.
            if assets.ready, let emblem = GameImages.template("emblem", height: 28) {
                Image(nsImage: emblem)
                    .renderingMode(.template)
                    .frame(width: 30, height: 30)
            } else {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .frame(width: 30, height: 30)
            }
            MText("Desktop Melee", size: 24)
            Spacer()
            MText("\(controller.step.rawValue) of \(WelcomeStep.allCases.count)", size: 17)
                .foregroundStyle(.secondary)
        }
        .padding(.bottom, 18)
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
            MText(title, size: 24)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 11)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(enabled ? PlayerColor.p1 : Color.primary.opacity(0.12))
                )
                .foregroundStyle(enabled ? Color.white : Color.secondary)
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .keyboardShortcut(.defaultAction)
    }
}

// MARK: - 1. Welcome

struct WelcomeIntroStep: View {
    @ObservedObject var controller: WelcomeController

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            DesktopSketch()
                .frame(height: 210)
            MText("Desktop Melee", size: 42)
            MParagraph("Real Melee, played right on your desktop. Your Dock is the stage, and the edges of the screen are the blast zones.",
                       size: 21, width: 392)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
            WelcomeButton("Get started") { controller.goTo(.disc) }
        }
    }
}

/// A desktop drawn with shapes (no Nintendo art): menu bar, two windows, the
/// Dock as the stage with P1 and P2 on it, and the screen edge dashed as the
/// blast zone.
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
                    .clipShape(UnevenRoundedRectangle(topLeadingRadius: 12, topTrailingRadius: 12))
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
                Circle().fill(PlayerColor.p1)
                    .frame(width: 16, height: 16)
                    .offset(x: w * 0.36, y: h - 43)
                Circle().fill(PlayerColor.p2)
                    .frame(width: 16, height: 16)
                    .offset(x: w * 0.6, y: h - 43)
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .strokeBorder(PlayerColor.p1.opacity(0.8), style: StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
            }
        }
        .accessibilityLabel("Your desktop: the Dock is the stage, the screen edges are the blast zones")
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
                       ? "Desktop Melee can't use the disc it played last time. Choose your Melee disc image again."
                       : "Desktop Melee includes the game's code but none of Nintendo's data. Characters, stages and sound come from your own copy of Melee.",
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
            status
            Spacer(minLength: 0)
            WelcomeButton("Continue", enabled: isVerified) { controller.acceptDisc() }
        }
    }

    private var title: String {
        controller.findAgain ? "Find your Melee disc again" : "Bring your own Melee"
    }

    private var isVerified: Bool {
        if case .verified = controller.disc { return true }
        return false
    }

    private var dropZone: some View {
        VStack(spacing: 6) {
            Image(systemName: "opticaldisc")
                .font(.system(size: 30))
                .foregroundStyle(.secondary)
            MText("Drop your disc image here", size: 21)
            MText("Melee USA 1.02:  .iso  .gcm  .ciso  .rvz", size: 15)
                .foregroundStyle(.secondary)
            Button { controller.chooseFile() } label: {
                MText(isFailed ? "Choose another..." : "Choose file...", size: 19)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 6)
                    .background(Capsule().fill(Color.primary.opacity(0.14)))
            }
            .buttonStyle(.plain)
            .padding(.top, 4)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color.primary.opacity(targeted ? 0.12 : 0.04))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(targeted ? PlayerColor.p1 : Color.primary.opacity(0.25),
                              style: StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
        )
        .modifier(DropTarget(targeted: $targeted, live: !controller.isSample) { controller.drop($0) })
    }

    private var isFailed: Bool {
        if case .failed = controller.disc { return true }
        return false
    }

    @ViewBuilder
    private var status: some View {
        switch controller.disc {
        case .empty:
            EmptyView()
        case .checking(let path, let percent):
            statusBox {
                row(symbol: "hourglass", color: .secondary,
                    title: "Checking your disc… \(percent)%", detail: fileName(path))
                ProgressBar(fraction: Double(percent) / 100)
            }
        case .verified(let path):
            statusBox {
                row(symbol: "checkmark.circle.fill", color: .green,
                    title: "Super Smash Bros. Melee · USA · 1.02 · verified", detail: fileName(path))
            }
        case .failed(let path, let message):
            statusBox {
                row(symbol: "xmark.octagon.fill", color: .red, title: message, detail: fileName(path))
            }
        }
    }

    private func fileName(_ path: String) -> String { (path as NSString).lastPathComponent }

    private func statusBox<C: View>(@ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 8) { content() }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color.primary.opacity(0.06))
            )
    }

    private func row(symbol: String, color: Color, title: String, detail: String) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: symbol)
                .foregroundStyle(color)
                .font(.system(size: 15))
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

/// Between the disc and step 3: the game boots and decodes its assets.
struct WelcomeLoading: View {
    var body: some View {
        VStack(spacing: 18) {
            Spacer()
            Image(systemName: "opticaldisc")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)
                .symbolEffect(.pulse)
            MText("Getting Melee ready...", size: 26)
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - 3. Controls

struct WelcomeControlsStep: View {
    @ObservedObject var controller: WelcomeController

    private static let playerColors = [PlayerColor.p1, PlayerColor.p2,
                                       Color(red: 0.95, green: 0.75, blue: 0.15),
                                       Color(red: 0.25, green: 0.70, blue: 0.35)]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            MText("Grab a controller", size: 31)
            HStack(spacing: 8) {
                card(.gameCube, symbol: "gamecontroller.fill", title: "GameCube",
                     detail: "Official adapter, or one in Wii U mode")
                card(.otherPad, symbol: "gamecontroller", title: "Xbox, PS, Switch",
                     detail: "USB or Bluetooth")
                card(.keyboard, symbol: "keyboard", title: "Keyboard",
                     detail: controller.keyboardOpen ? "Hide the keys" : "See the keys")
                    .onTapGesture { controller.keyboardOpen.toggle() }
            }
            if controller.keyboardOpen {
                keyboardKeys
            } else {
                presses
            }
            Spacer(minLength: 0)
            WelcomeButton("Continue") { controller.goTo(.permission) }
        }
    }

    private func color(_ port: Int) -> Color { Self.playerColors[min(port, 3)] }

    /// The first player whose press matches this kind of device.
    private func lit(_ kind: WelcomeController.DeviceKind) -> Int? {
        controller.pressed.first { WelcomeController.kind(of: $0.name) == kind }?.port
    }

    private func card(_ kind: WelcomeController.DeviceKind, symbol: String, title: String,
                      detail: String) -> some View {
        let port = lit(kind)
        return VStack(spacing: 5) {
            Image(systemName: symbol)
                .font(.system(size: 22))
                .foregroundStyle(port.map { color($0) } ?? .secondary)
                .frame(height: 26)
            MText(title, size: fittedSize(title, size: 17, width: 112))
            MParagraph(detail, size: 14, width: 112)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, minHeight: 100, alignment: .top)
        .padding(.vertical, 8)
        .padding(.horizontal, 6)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color.primary.opacity(0.06))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(port.map { color($0) } ?? .clear, lineWidth: 2)
        )
        .contentShape(Rectangle())
    }

    private var presses: some View {
        Section(title: "Press any button") {
            if controller.pressed.isEmpty {
                MParagraph("Press a button on each controller, or a key, to see who's who.",
                           size: 17, width: 360)
                    .foregroundStyle(.secondary)
            }
            ForEach(controller.pressed) { press in
                HStack(spacing: 10) {
                    MText("P\(press.port + 1)", size: 22)
                        .foregroundStyle(color(press.port))
                    MText(press.name, size: fittedSize(press.name, size: 19, width: 270))
                    Spacer()
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                }
            }
            MParagraph("Ports can be changed in the menu bar's Controls tab.", size: 15, width: 360)
                .foregroundStyle(.secondary)
        }
    }

    private func key(_ slot: Int32) -> String { String(cString: pet_key_name(slot)) }

    /// The stick and C-stick keys, read live from the bindings.
    private var stickKeys: String {
        let keys = [14, 13, 15, 12].map { key(Int32($0)) } // up left down right
        if keys == ["Up", "Left", "Down", "Right"] { return "Arrow keys or W A S D" }
        return keys.joined(separator: " ")
    }

    private var cStickKeys: String {
        [18, 17, 19, 16].map { key(Int32($0)) }.joined(separator: " ")
    }

    private var keyboardKeys: some View {
        Section(title: "Keyboard") {
            let buttons: [(badge: Int, slot: Int32)] = [(0, 0), (1, 1), (2, 2), (3, 3), (4, 5), (5, 6), (6, 7)]
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading, spacing: 3) {
                ForEach(buttons, id: \.badge) { b in
                    HStack(spacing: 8) {
                        PadBadge(button: b.badge)
                        MText(key(b.slot), size: 18)
                    }
                }
                HStack(spacing: 8) {
                    MText("Start", size: 16).foregroundStyle(.secondary)
                    MText(key(4), size: 18)
                }
            }
            keyRow("Stick", stickKeys)
            keyRow("C-stick", cStickKeys)
            MParagraph("The keyboard works while Desktop Melee is the active app.", size: 15, width: 360)
                .foregroundStyle(.secondary)
        }
    }

    private func keyRow(_ label: String, _ keys: String) -> some View {
        HStack(spacing: 10) {
            MText(label, size: 16).foregroundStyle(.secondary)
                .frame(width: 70, alignment: .leading)
            MText(keys, size: fittedSize(keys, size: 18, width: 280))
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

// MARK: - 4. Permission

struct WelcomePermissionStep: View {
    @ObservedObject var controller: WelcomeController
    private static let title = "Let Desktop Melee measure your Dock"

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            MText(Self.title, size: fittedSize(Self.title, size: 31, width: 392))
            MParagraph("With Accessibility turned on, the stage matches your Dock to the pixel. Desktop Melee only reads where the Dock is.",
                       size: 19, width: 392)
                .foregroundStyle(.secondary)
            Section(title: "Accessibility") {
                HStack(spacing: 10) {
                    Image(systemName: controller.axGranted ? "checkmark.circle.fill" : "circle.dashed")
                        .foregroundStyle(controller.axGranted ? Color.green : Color.orange)
                    MText(controller.axGranted ? "Turned on: the Dock is measured exactly"
                                               : "Waiting for permission...", size: 18)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            Spacer(minLength: 0)
            WelcomeButton("Open System Settings") { controller.requestAccessibility() }
            Button { controller.goTo(.ready) } label: {
                MText("Not now: the Dock size will be estimated", size: 16)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.plain)
        }
    }
}

// MARK: - 5. Ready

struct WelcomeReadyStep: View {
    @ObservedObject var controller: WelcomeController

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            MenuBarSketch()
                .frame(height: 120)
            MText("You're set", size: 42)
            MParagraph("Desktop Melee lives in your menu bar. Use it to pick characters, change controls and start a match.",
                       size: 21, width: 392)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
            WelcomeButton("Start a match") { controller.startMatch() }
            MText("or press Start on your controller", size: 16)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity)
        }
    }
}

/// The top of a screen: the menu bar, with an arrow at the Smash icon.
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
                    .clipShape(UnevenRoundedRectangle(topLeadingRadius: 12, topTrailingRadius: 12))
                // Other menu bar items, then the Smash icon.
                ForEach(0..<3) { i in
                    Capsule().fill(Color(white: 0.35))
                        .frame(width: 18, height: 8)
                        .offset(x: w - 40 - CGFloat(i) * 28, y: 11)
                }
                Group {
                    if assets.ready, let emblem = GameImages.template("emblem", height: 18) {
                        Image(nsImage: emblem).renderingMode(.template)
                    } else {
                        Image(nsImage: SmashEmblem.image(size: 18)).renderingMode(.template)
                    }
                }
                .foregroundStyle(.white)
                .frame(width: 22, height: 22)
                .offset(x: w - 150, y: 4)
                Image(systemName: "arrow.up")
                    .font(.system(size: 30, weight: .bold))
                    .foregroundStyle(PlayerColor.p1)
                    .offset(x: w - 154, y: 40)
                MText("Smash icon", size: 18)
                    .foregroundStyle(.secondary)
                    .offset(x: w - 262, y: 74)
            }
        }
    }
}
