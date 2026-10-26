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

    init() {
        let d = Self.defaults
        stocks = d.object(forKey: "match.stocks") as? Int ?? 4
        minutes = d.object(forKey: "match.minutes") as? Int ?? 7
        push()
    }

    func refresh() {
        active = pet_match_active()
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
                match.startOrRestart()
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: match.active ? "arrow.counterclockwise" : "play.fill")
                    Text(match.active ? "Restart Match" : "Start Match")
                        .font(.system(size: 14, weight: .bold))
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

            Section(title: "Rules") {
                StepperRow(symbol: "heart.fill", label: "Stocks",
                           value: "\(match.stocks)") {
                    match.stocks = max(1, match.stocks - 1)
                } increment: {
                    match.stocks = min(99, match.stocks + 1)
                }
                StepperRow(symbol: "timer", label: "Time",
                           value: match.minutes == 0 ? "None" : "\(match.minutes):00") {
                    match.minutes = max(0, match.minutes - 1)
                } increment: {
                    match.minutes = min(99, match.minutes + 1)
                }
                HStack(spacing: 10) {
                    Image(systemName: "shippingbox")
                        .frame(width: 18)
                        .foregroundStyle(.secondary)
                    Text("Items")
                    Spacer()
                    Text("Off")
                        .foregroundStyle(.secondary)
                }
            }

            Section(title: "Time-out") {
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: "flag.2.crossed")
                        .frame(width: 18)
                        .foregroundStyle(.secondary)
                    Text("When time runs out, the match ends and the pet goes back to idle.")
                        .font(.system(size: 12))
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Text("Rule changes apply to the next match.")
                .font(.system(size: 11))
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
    let decrement: () -> Void
    let increment: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol)
                .frame(width: 18)
                .foregroundStyle(.secondary)
            Text(label)
            Spacer()
            Button(action: decrement) { Image(systemName: "minus") }
                .buttonStyle(.borderless)
            Text(value)
                .font(.system(size: 13, weight: .semibold).monospacedDigit())
                .frame(minWidth: 44)
            Button(action: increment) { Image(systemName: "plus") }
                .buttonStyle(.borderless)
        }
    }
}
