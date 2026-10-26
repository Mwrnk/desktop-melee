// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window's steps, in the popover's dark section style and Melee's
// font (bundled with the app, so it is there before the disc). The emblem and
// sounds still come from the disc.
import AppKit
import SwiftUI

struct WelcomeView: View {
    @ObservedObject var controller: WelcomeController

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            Group {
                switch controller.step {
                case .welcome: WelcomeIntroStep(controller: controller)
                case .disc: WelcomeDiscStep(controller: controller)
                default: EmptyView()
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
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 30, height: 30)
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
