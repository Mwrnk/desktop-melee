// SPDX-License-Identifier: GPL-3.0-or-later
// The melee-pet popover. Step 1 of pet/menubar-design.md: the shell, the tab
// bar and the Sound tab. Text uses the system font until Melee's own glyphs
// are decoded (step 3).
import PetBridge
import SwiftUI

// MARK: - Settings

/// Melee's sound settings, shared with melee-pc's launcher and F1 menu
/// (launcher.cfg). Changes apply live; they are saved when a slider is
/// released or a switch flips.
final class AudioSettings: ObservableObject {
    @Published var volume: Double = 1
    @Published var music: Double = 1
    @Published var sfx: Double = 1
    @Published var mute = false
    @Published var reverb = true

    func load() {
        var v: Float = 1, m: Float = 1, s: Float = 1
        var muted = false, rev = true
        pc_prefs_get_audio(&v, &m, &s, &muted, &rev)
        volume = Double(v)
        music = Double(m)
        sfx = Double(s)
        mute = muted
        reverb = rev
    }

    func apply() {
        pc_prefs_set_audio(Float(volume), Float(music), Float(sfx), mute, reverb)
    }

    func save() {
        apply()
        pc_prefs_save()
    }
}

// MARK: - Tabs

enum PetTab: String, CaseIterable, Identifiable {
    case match = "Match"
    case players = "Players"
    case controls = "Controls"
    case sound = "Sound"
    case display = "Display"

    var id: String { rawValue }

    var symbol: String {
        switch self {
        case .match: return "flag.checkered"
        case .players: return "person.2.fill"
        case .controls: return "gamecontroller.fill"
        case .sound: return "speaker.wave.2.fill"
        case .display: return "display"
        }
    }

    /// The build step (pet/menubar-design.md) that fills this tab in.
    var step: Int {
        switch self {
        case .match: return 2
        case .players: return 4
        case .controls: return 5
        case .sound: return 1
        case .display: return 6
        }
    }
}

// MARK: - Panel

struct PetPanel: View {
    @ObservedObject var audio: AudioSettings
    @State private var tab: PetTab = .sound

    var body: some View {
        VStack(spacing: 0) {
            header
            tabBar
                .padding(.horizontal, 12)
                .padding(.bottom, 10)
            Divider()
            Group {
                switch tab {
                case .sound: SoundTab(audio: audio)
                default: ComingSoon(tab: tab)
                }
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
            .padding(12)
            Divider()
            footer
        }
        .frame(width: 360)
    }

    private var header: some View {
        HStack(spacing: 10) {
            Image(nsImage: SmashEmblem.image(size: 28))
                .renderingMode(.template)
                .foregroundStyle(.primary)
            VStack(alignment: .leading, spacing: 1) {
                Text("melee pet")
                    .font(.system(size: 15, weight: .heavy))
                Text("Running on your desktop")
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(.horizontal, 14)
        .padding(.top, 14)
        .padding(.bottom, 10)
    }

    private var tabBar: some View {
        HStack(spacing: 4) {
            ForEach(PetTab.allCases) { item in
                Button {
                    tab = item
                } label: {
                    VStack(spacing: 3) {
                        Image(systemName: item.symbol)
                            .font(.system(size: 14, weight: .semibold))
                            .frame(height: 16)
                        Text(item.rawValue)
                            .font(.system(size: 10, weight: .medium))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 6)
                    .background(
                        RoundedRectangle(cornerRadius: 8, style: .continuous)
                            .fill(tab == item ? Color.accentColor.opacity(0.18) : .clear)
                    )
                    .foregroundStyle(tab == item ? Color.accentColor : .secondary)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
    }

    private var footer: some View {
        HStack {
            Text("melee-pet")
                .font(.system(size: 11))
                .foregroundStyle(.tertiary)
            Spacer()
            Button("Quit") { pet_request_quit() }
                .keyboardShortcut("q")
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
    }
}

// MARK: - Sections

/// A Control Center-style rounded group.
struct Section<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased())
                .font(.system(size: 10, weight: .semibold))
                .foregroundStyle(.secondary)
            VStack(spacing: 10) { content }
                .padding(12)
                .background(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .fill(Color.primary.opacity(0.06))
                )
        }
    }
}

// MARK: - Sound

struct SoundTab: View {
    @ObservedObject var audio: AudioSettings

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Section(title: "Volume") {
                VolumeRow(symbol: "speaker.wave.2.fill", label: "Master", value: $audio.volume, audio: audio)
                    .disabled(audio.mute)
                    .opacity(audio.mute ? 0.45 : 1)
                VolumeRow(symbol: "music.note", label: "Music", value: $audio.music, audio: audio)
                VolumeRow(symbol: "burst.fill", label: "Effects", value: $audio.sfx, audio: audio)
            }
            Section(title: "Options") {
                Toggle(isOn: $audio.mute) {
                    Label("Mute", systemImage: "speaker.slash.fill")
                }
                .onChange(of: audio.mute) { _ in audio.save() }
                Toggle(isOn: $audio.reverb) {
                    Label("Reverb", systemImage: "waveform")
                }
                .onChange(of: audio.reverb) { _ in audio.save() }
            }
            .toggleStyle(.switch)
        }
    }
}

struct VolumeRow: View {
    let symbol: String
    let label: String
    @Binding var value: Double
    let audio: AudioSettings

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol)
                .frame(width: 18)
                .foregroundStyle(.secondary)
            Text(label)
                .frame(width: 52, alignment: .leading)
            Slider(value: $value, in: 0...1) { editing in
                if !editing { audio.save() }
            }
            .onChange(of: value) { _ in audio.apply() }
            Text("\(Int((value * 100).rounded()))%")
                .font(.system(size: 11).monospacedDigit())
                .foregroundStyle(.secondary)
                .frame(width: 36, alignment: .trailing)
        }
    }
}

// MARK: - Placeholder

struct ComingSoon: View {
    let tab: PetTab

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: tab.symbol)
                .font(.system(size: 28, weight: .semibold))
                .foregroundStyle(.tertiary)
            Text(tab.rawValue)
                .font(.system(size: 13, weight: .semibold))
            Text("Coming in step \(tab.step)")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, minHeight: 180)
    }
}
