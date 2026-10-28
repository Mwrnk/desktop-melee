// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window's debug panel (DESKTOP_MELEE_WELCOME=debug): jump to any
// step and state, fake presses, discs and the loading wait, play the motion in
// slow motion and tune it. It drives a separate welcome window whose actions
// reach neither the game nor the saved settings (WelcomeController.debug).
import AppKit
import SwiftUI

enum WelcomeDebugPanel {
    private static var panel: NSPanel?

    static func show(for controller: WelcomeController) {
        if panel == nil {
            let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 320, height: 700),
                                styleMask: [.titled, .closable, .utilityWindow],
                                backing: .buffered, defer: false)
            panel.title = "Welcome debug"
            panel.isReleasedWhenClosed = false
            panel.hidesOnDeactivate = false
            panel.becomesKeyOnlyIfNeeded = true
            panel.level = .popUpMenu
            panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
            let hosting = NSHostingView(rootView: WelcomeDebugView(controller: controller))
            hosting.sizingOptions = [] // see WelcomeController.makeWindow
            panel.contentView = hosting
            self.panel = panel
        }
        guard let panel else { return }
        // To the right of the welcome window.
        if let frame = controller.debugWindowFrame {
            panel.setFrameTopLeftPoint(NSPoint(x: frame.maxX + 16, y: frame.maxY))
        }
        panel.orderFrontRegardless()
    }
}

struct WelcomeDebugView: View {
    @ObservedObject var controller: WelcomeController
    @ObservedObject private var motion = WelcomeMotion.shared
    @ObservedObject private var assets = GameAssets.shared

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                screens
                disc
                players
                motionControls
            }
            .padding(14)
        }
        .frame(width: 320)
        .frame(minHeight: 400, idealHeight: 700)
        .font(.system(size: 12))
        .controlSize(.small)
    }

    private func box<C: View>(_ title: String, @ViewBuilder _ content: () -> C) -> some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 8) { content() }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(4)
        } label: {
            Text(title).font(.system(size: 11, weight: .semibold)).foregroundStyle(.secondary)
        }
    }

    private var screens: some View {
        box("Screen") {
            HStack(spacing: 4) {
                ForEach(WelcomeStep.allCases, id: \.self) { step in
                    Button("\(step.rawValue)") { controller.jump(to: step) }
                        .buttonStyle(.bordered)
                        .tint(controller.step == step && !controller.loading ? .accentColor : nil)
                }
                Button("Loading") { controller.simulateLoading() }
                    .buttonStyle(.bordered)
                    .tint(controller.loading ? .accentColor : nil)
            }
            HStack {
                Button("Replay") { controller.replay += 1 }
                Button("Open window") { controller.showDebugWindow() }
            }
            Toggle("Before the disc (system font, no emblem, no sounds)",
                   isOn: Binding(get: { assets.debugBeforeDisc },
                                 set: { on in withAnimation(motion.step) {
                                     assets.debugBeforeDisc = on } }))
            Text("Loading ends with the reveal, which turns this off.")
                .foregroundStyle(.secondary)
        }
    }

    private var disc: some View {
        box("Disc") {
            HStack(spacing: 4) {
                Button("Empty") { controller.setDisc(.empty) }
                Button("Check") { controller.simulateCheck() }
                Button("Verified") {
                    controller.setDisc(.verified(path: "/Users/you/Downloads/Melee.iso"))
                }
                Button("Refused") { controller.simulateRefusal() }
            }
            Toggle("Find it again (later launches)", isOn: Binding(
                get: { controller.findAgain },
                set: { on in
                    controller.findAgain = on
                    controller.firstRun = !on
                    controller.oldPath = on ? "/Users/you/Downloads/Melee.iso" : nil
                }))
            Text("Choose file and drops check a real file.").foregroundStyle(.secondary)
        }
    }

    private var players: some View {
        box("Controllers") {
            HStack(spacing: 4) {
                Button("GameCube") { controller.simulatePress("GameCube controller") }
                Button("DualSense") { controller.simulatePress("DualSense Wireless Controller") }
                Button("Xbox") { controller.simulatePress("Xbox Wireless Controller") }
                Button("Keys") { controller.simulatePress("Keyboard") }
            }
            HStack {
                Button("Clear") { controller.pressed = [] }
                Toggle("Keyboard keys open", isOn: $controller.keyboardOpen)
            }
            HStack {
                Button("Accessibility waiting") { controller.axGranted = false }
                Button("Granted") { controller.accessibilityGranted() }
            }
            Text("Real controllers and keys show up on step 3 too.").foregroundStyle(.secondary)
        }
    }

    private var motionControls: some View {
        box("Motion") {
            slider("Speed", value: $motion.speed, in: 0.1...1.5, format: "%.2f×")
            HStack {
                ForEach([0.1, 0.25, 0.5, 1.0], id: \.self) { s in
                    Button(s == 1 ? "1×" : String(format: "%.2g×", s)) { motion.speed = s }
                }
            }
            Toggle("Reduce Motion", isOn: $motion.forceReduce)
            Divider()
            slider("Step", value: $motion.values.stepDuration, in: 0.1...0.8, format: "%.2f s")
            slider("Slide", value: $motion.values.stepSlide, in: 0...80, format: "%.0f pt")
            slider("Pop", value: $motion.values.popDuration, in: 0.15...1, format: "%.2f s")
            slider("Bounce", value: $motion.values.popBounce, in: 0...0.7, format: "%.2f")
            slider("Stagger", value: $motion.values.stagger, in: 0...0.2, format: "%.3f s")
            HStack {
                Button("Reset") {
                    motion.values = WelcomeMotion.Values()
                    motion.speed = 1
                    motion.forceReduce = false
                }
                Button("Copy values") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(motion.summary, forType: .string)
                    NSLog("desktop-melee: welcome motion: %@", motion.summary)
                }
            }
            Text(motion.summary).foregroundStyle(.secondary).textSelection(.enabled)
        }
    }

    private func slider(_ label: String, value: Binding<Double>, in range: ClosedRange<Double>,
                        format: String) -> some View {
        HStack {
            Text(label).frame(width: 52, alignment: .leading)
            Slider(value: value, in: range)
            Text(String(format: format, value.wrappedValue))
                .monospacedDigit()
                .frame(width: 56, alignment: .trailing)
        }
    }
}
