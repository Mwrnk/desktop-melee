// SPDX-License-Identifier: GPL-3.0-or-later
// The Match tab: start or restart a match, and its rules.
import PetBridge
import SwiftUI

/// The match rules, remembered across launches and handed to the game's
/// GM_PET mode (pet_set_match_setup) whenever they change. The players part
/// of the setup is left as the game has it (the Players tab owns it).
final class MatchSettings: ObservableObject {
    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard

    @Published var stocks: Int {
        didSet { Self.defaults.set(stocks, forKey: "match.stocks"); push() }
    }
    /// 0 = no time limit.
    @Published var minutes: Int {
        didSet { Self.defaults.set(minutes, forKey: "match.minutes"); push() }
    }
    @Published private(set) var active = false
    /// Pause and hide is on: the fighters are hidden.
    @Published private(set) var hidden = false

    init() {
        let d = Self.defaults
        stocks = d.object(forKey: "match.stocks") as? Int ?? 4
        minutes = d.object(forKey: "match.minutes") as? Int ?? 7
        push()
    }

    func refresh() {
        active = pet_match_active()
        hidden = pet_hidden()
    }

    func startOrRestart() {
        pet_request_match()
    }

    private func push() {
        var setup = PetMatchSetup()
        pet_match_setup(&setup)
        setup.stocks = Int32(max(1, min(99, stocks)))
        setup.time_seconds = Int32(max(0, minutes) * 60)
        pet_set_match_setup(&setup)
    }
}

struct MatchTab: View {
    @ObservedObject var match: MatchSettings
    private let poll = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Button {
                MenuSound.confirm.play()
                match.startOrRestart()
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: match.active ? "arrow.counterclockwise" : "play.fill")
                    MText(match.active ? "Restart Match" : "Start Match", size: 22)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(match.active ? Color.orange : Color.accentColor)
                )
                .foregroundStyle(.white)
            }
            .buttonStyle(.plain)

            PauseAndHideRow(hidden: match.hidden)

            Section(title: "Rules") {
                StepperRow(symbol: "heart.fill", label: "Stocks",
                           value: "\(match.stocks)",
                           canDecrement: match.stocks > 1, canIncrement: match.stocks < 99) {
                    match.stocks = max(1, match.stocks - 1)
                } increment: {
                    match.stocks = min(99, match.stocks + 1)
                }
                StepperRow(symbol: "timer", label: "Time",
                           value: match.minutes == 0 ? "None" : "\(match.minutes):00",
                           canDecrement: match.minutes > 0, canIncrement: match.minutes < 99) {
                    match.minutes = max(0, match.minutes - 1)
                } increment: {
                    match.minutes = min(99, match.minutes + 1)
                }
                HStack(spacing: 10) {
                    Image(systemName: "shippingbox")
                        .frame(width: 18)
                        .foregroundStyle(.secondary)
                    MText("Items", size: 19)
                    Spacer()
                    MText("Off", size: 19)
                        .foregroundStyle(.secondary)
                }
            }

            Section(title: "Time-out") {
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: "flag.2.crossed")
                        .frame(width: 18)
                        .foregroundStyle(.secondary)
                    MParagraph("When time runs out, the match ends and the pet goes back to idle.",
                               size: 17, width: 262)
                        .foregroundStyle(.secondary)
                }
            }

            MText("Rule changes apply to the next match.", size: 15)
                .foregroundStyle(.tertiary)
        }
        .onAppear { match.refresh() }
        .onReceive(poll) { _ in match.refresh() }
    }
}

struct StepperRow: View {
    let symbol: String
    let label: String
    let value: String
    var canDecrement = true
    var canIncrement = true
    let decrement: () -> Void
    let increment: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol)
                .frame(width: 18)
                .foregroundStyle(.secondary)
            MText(label, size: 19)
            Spacer()
            HStack(spacing: 6) {
                StepButton(symbol: "minus", enabled: canDecrement) {
                    MenuSound.move.play()
                    decrement()
                }
                MText(value, size: 20)
                    .frame(minWidth: 44)
                StepButton(symbol: "plus", enabled: canIncrement) {
                    MenuSound.move.play()
                    increment()
                }
            }
        }
    }
}

/// Pause and hide, under Start: a button, and its global shortcut, which
/// can be changed by clicking it and pressing new keys.
struct PauseAndHideRow: View {
    let hidden: Bool
    @State private var shortcut = PauseHotkey.display
    @State private var recording = false
    @State private var taken = false
    @State private var monitor: Any?

    var body: some View {
        HStack(spacing: 10) {
            Button {
                MenuSound.confirm.play()
                pet_toggle_pause_and_hide()
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: hidden ? "eye" : "eye.slash")
                    MText(hidden ? "Show" : "Pause and hide", size: 19)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 7)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(Color.primary.opacity(0.1))
                )
            }
            .buttonStyle(.plain)
            Button {
                recording ? stopRecording() : startRecording()
            } label: {
                MText(recording ? "Press keys..." : taken ? "Taken" : shortcut, size: 18)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 7)
                    .background(
                        RoundedRectangle(cornerRadius: 8, style: .continuous)
                            .strokeBorder(recording ? Color.accentColor : Color.primary.opacity(0.25),
                                          lineWidth: 1.5)
                    )
            }
            .buttonStyle(.plain)
            .help("Click, then press a new shortcut with two modifier keys (such as Control-Option)")
        }
        .onReceive(NotificationCenter.default.publisher(for: PauseHotkey.changed)) { _ in
            shortcut = PauseHotkey.display
        }
        // The popover closed mid-recording: stop listening for keys.
        .onReceive(NotificationCenter.default.publisher(for: MenuBarController.popoverClosed)) { _ in
            stopRecording()
        }
        .onDisappear { stopRecording() }
    }

    private func startRecording() {
        recording = true
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if event.keyCode == 53 { // Escape: keep the old shortcut
                stopRecording()
                return nil
            }
            guard let mods = PauseHotkey.carbonMods(event.modifierFlags) else {
                NSSound.beep()
                return nil
            }
            if PauseHotkey.set(keyCode: UInt32(event.keyCode), mods: mods,
                               label: PauseHotkey.label(for: event)) {
                MenuSound.confirm.play()
            } else {
                // Another app has it: say so for a moment; the old one stays.
                MenuSound.back.play()
                taken = true
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { taken = false }
            }
            stopRecording()
            return nil
        }
    }

    private func stopRecording() {
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
        recording = false
    }
}
