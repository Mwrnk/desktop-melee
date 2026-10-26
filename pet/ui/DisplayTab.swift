// SPDX-License-Identifier: GPL-3.0-or-later
// The Display tab: fighter size, the notch island and which screen to play
// on. Size and island apply at the next match (the floor is stretched to the
// Dock when a stage loads); the screen changes right away, or when the
// running match ends.
import AppKit
import Combine
import PetBridge
import SwiftUI

// MARK: - Settings

final class DisplaySettings: ObservableObject {
    /// Fighter size in percent (the blastzones are the screen edges at any
    /// size, so smaller fighters just get more room).
    static let sizes: ClosedRange<Double> = 50...200

    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard

    @Published private(set) var size: Double
    @Published private(set) var island: Bool
    /// The chosen screen's name; "" follows the primary screen.
    @Published private(set) var screen: String
    @Published private(set) var screens: [String] = []
    @Published private(set) var primary = ""
    let sizeLocked = pet_fighter_size_locked()

    private var screenChanges: AnyCancellable?

    init() {
        let d = Self.defaults
        let stored = d.object(forKey: "display.size") as? Double ?? 100
        size = min(max(stored, Self.sizes.lowerBound), Self.sizes.upperBound)
        island = d.object(forKey: "display.island") as? Bool ?? true
        screen = d.string(forKey: "display.screen") ?? ""
        refreshScreens()
        pet_set_fighter_size(Float(size))
        pet_set_island_enabled(island)
        pet_set_screen(screen)
        // A display plugged in or out, or the Dock resized: the overlay goes
        // back to the chosen screen (or the primary while it is missing) and
        // is laid out again. AppKit also posts this when nothing changed (over
        // and over while a modal alert is up, since laying out re-triggers
        // it), so only act on a real change.
        screenLayout = Self.screenLayout()
        screenChanges = NotificationCenter.default
            .publisher(for: NSApplication.didChangeScreenParametersNotification)
            .sink { [weak self] _ in
                guard let self else { return }
                let layout = Self.screenLayout()
                guard layout != self.screenLayout else { return }
                self.screenLayout = layout
                self.refreshScreens()
                pet_set_screen(self.screen)
            }
    }

    private var screenLayout: [String] = []

    /// Each screen's name, frame and usable area.
    private static func screenLayout() -> [String] {
        NSScreen.screens.map {
            "\($0.localizedName) \(NSStringFromRect($0.frame)) \(NSStringFromRect($0.visibleFrame))"
        }
    }

    func refreshScreens() {
        let names = NSScreen.screens.map(\.localizedName)
        if screens != names { screens = names }
        let first = names.first ?? ""
        if primary != first { primary = first }
    }

    /// The screen the overlay is on: the chosen one if connected.
    var current: String {
        screens.contains(screen) ? screen : primary
    }

    func setSize(_ value: Double) {
        let snapped = (value / 10).rounded() * 10
        guard snapped != size else { return }
        size = snapped
        Self.defaults.set(size, forKey: "display.size")
        pet_set_fighter_size(Float(size))
        MenuSound.move.play()
    }

    func setIsland(_ on: Bool) {
        island = on
        Self.defaults.set(on, forKey: "display.island")
        pet_set_island_enabled(on)
        MenuSound.confirm.play()
    }

    func setScreen(_ name: String) {
        // The primary screen is stored as "", so it keeps following the
        // primary if the arrangement changes.
        let value = name == primary ? "" : name
        guard value != screen || name != current else { return }
        screen = value
        Self.defaults.set(value, forKey: "display.screen")
        pet_set_screen(value)
        MenuSound.confirm.play()
    }
}

// MARK: - Tab

struct DisplayTab: View {
    @ObservedObject var display: DisplaySettings
    @ObservedObject var match: MatchSettings
    private let poll = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Section(title: "Fighters") {
                HStack(spacing: 10) {
                    Image(systemName: "figure.stand")
                        .frame(width: 18)
                        .foregroundStyle(.secondary)
                    MText("Size", size: 19)
                    Spacer()
                    MText("\(Int(display.size))%", size: 19)
                        .foregroundStyle(.secondary)
                }
                HStack(spacing: 8) {
                    Image(systemName: "figure.stand")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                    Slider(value: Binding(get: { display.size }, set: { display.setSize($0) }),
                           in: DisplaySettings.sizes, step: 10)
                    Image(systemName: "figure.stand")
                        .font(.system(size: 17))
                        .foregroundStyle(.secondary)
                }
                .disabled(display.sizeLocked)
                MParagraph(display.sizeLocked
                           ? "Set by MELEE_PET_SCALE in the environment."
                           : "The screen edges are the blastzones: bigger fighters are KO'd sooner.",
                           size: 15, width: 300)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            Section(title: "Notch island") {
                Toggle(isOn: Binding(get: { display.island }, set: { display.setIsland($0) })) {
                    OptionLabel(symbol: "rectangle.topthird.inset.filled", text: "Stocks in the notch")
                }
                .toggleStyle(.switch)
                .frame(maxWidth: .infinity, alignment: .leading)
                MParagraph(display.island
                           ? "Stocks and percents sit beside the notch, in a black island."
                           : "Melee's own stocks and percents, at the bottom of the screen.",
                           size: 15, width: 300)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            Section(title: "Screen") {
                VStack(spacing: 4) {
                    ForEach(display.screens, id: \.self) { name in
                        ScreenRow(name: name, primary: name == display.primary,
                                  selected: name == display.current) {
                            display.setScreen(name)
                        }
                    }
                }
                if display.screens.count < 2 {
                    MParagraph("Connect another display to play on it.", size: 15, width: 300)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }

            MParagraph(match.active
                       ? "Size and island apply at the next match, and so does a new screen."
                       : "Size and island apply when the match starts.",
                       size: 15, width: 330)
                .foregroundStyle(.tertiary)
        }
        .onAppear {
            display.refreshScreens()
            match.refresh()
        }
        .onReceive(poll) { _ in match.refresh() }
    }
}

/// One connected screen, with a checkmark on the one the pet plays on.
struct ScreenRow: View {
    let name: String
    let primary: Bool
    let selected: Bool
    let select: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: select) {
            HStack(spacing: 10) {
                Image(systemName: primary ? "laptopcomputer" : "display")
                    .frame(width: 18)
                    .foregroundStyle(.secondary)
                MText(name, size: fittedSize(name, size: 17, width: primary ? 170 : 230))
                if primary {
                    MText("main", size: 14)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Capsule().fill(Color.primary.opacity(0.08)))
                }
                Spacer(minLength: 0)
                Image(systemName: "checkmark")
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(Color.accentColor)
                    .opacity(selected ? 1 : 0)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(Color.primary.opacity(selected ? 0.1 : hovering ? 0.06 : 0))
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}
