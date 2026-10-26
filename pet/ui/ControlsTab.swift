// SPDX-License-Identifier: GPL-3.0-or-later
// The Controls tab: per player, the controller port, tap jump, Z = jump and
// a button map; for everyone, UCF. Button changes apply mid-match (the game
// reads them every frame); the port applies at the next match.
import PetBridge
import SwiftUI

// MARK: - Settings

final class ControlsSettings: ObservableObject {
    /// The Melee buttons a map covers, in PET_BTN_* order.
    static let buttons = ["A", "B", "X", "Y", "Z", "L", "R"]
    static let none = buttons.count
    static let x = 2, z = 4

    struct Player: Equatable {
        var tapJump = true
        /// Physical button -> the Melee button it acts as (or `none`).
        var map = Array(0..<ControlsSettings.buttons.count)
        var port: Int

        var zJump: Bool { map[ControlsSettings.z] == ControlsSettings.x }
        var isDefault: Bool { tapJump && map == Array(0..<ControlsSettings.buttons.count) }
    }

    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard

    @Published private(set) var players: [Player]
    @Published var editing = 0
    @Published private(set) var ucf = false
    @Published private(set) var ucfLocked = false
    /// What is plugged into ports 1-4 ("" for nothing).
    @Published private(set) var devices = ["", "", "", ""]

    init() {
        let d = Self.defaults
        players = (0..<2).map { i in
            let key = "controls.p\(i + 1)."
            var p = Player(port: i)
            p.tapJump = d.object(forKey: key + "tapjump") as? Bool ?? true
            if let map = d.array(forKey: key + "map") as? [Int], map.count == Self.buttons.count,
               map.allSatisfy({ (0...Self.none).contains($0) }) {
                p.map = map
            }
            if let port = d.object(forKey: key + "port") as? Int, (0..<4).contains(port) {
                p.port = port
            }
            return p
        }
        if players[0].port == players[1].port { players[1].port = players[0].port == 1 ? 0 : 1 }
        push()
        refresh()
    }

    func refresh() {
        var on = false, locked = false
        pc_prefs_get_ucf(&on, &locked)
        if ucf != on { ucf = on }
        if ucfLocked != locked { ucfLocked = locked }
        let now = (0..<4).map { String(cString: pet_port_device(Int32($0))) }
        if devices != now { devices = now }
    }

    func setTapJump(_ i: Int, _ on: Bool) {
        players[i].tapJump = on
        commit()
        MenuSound.confirm.play()
    }

    func setZJump(_ i: Int, _ on: Bool) {
        players[i].map[Self.z] = on ? Self.x : Self.z
        commit()
        MenuSound.confirm.play()
    }

    func setMap(_ i: Int, button: Int, target: Int) {
        guard players[i].map[button] != target else { return }
        players[i].map[button] = target
        commit()
        MenuSound.confirm.play()
    }

    func reset(_ i: Int) {
        players[i].tapJump = true
        players[i].map = Array(0..<Self.buttons.count)
        commit()
        MenuSound.back.play()
    }

    /// Two players never share a port: taking the other's swaps them.
    func setPort(_ i: Int, _ port: Int) {
        guard players[i].port != port else { return }
        if players[1 - i].port == port { players[1 - i].port = players[i].port }
        players[i].port = port
        commit()
        MenuSound.confirm.play()
    }

    func setUCF(_ on: Bool) {
        guard !ucfLocked else { return }
        pc_prefs_set_ucf(on)
        pc_prefs_save()
        refresh()
        MenuSound.confirm.play()
    }

    private func commit() {
        let d = Self.defaults
        for (i, p) in players.enumerated() {
            let key = "controls.p\(i + 1)."
            d.set(p.tapJump, forKey: key + "tapjump")
            d.set(p.map, forKey: key + "map")
            d.set(p.port, forKey: key + "port")
        }
        push()
    }

    private func push() {
        for (i, p) in players.enumerated() {
            pet_set_tap_jump(Int32(i), p.tapJump)
            for (b, t) in p.map.enumerated() {
                pet_set_button_map(Int32(i), Int32(b), Int32(t))
            }
        }
        var setup = PetMatchSetup()
        pet_match_setup(&setup)
        setup.players.0.port = Int32(players[0].port)
        setup.players.1.port = Int32(players[1].port)
        pet_set_match_setup(&setup)
    }
}

// MARK: - Tab

struct ControlsTab: View {
    @ObservedObject var controls: ControlsSettings
    @ObservedObject var players: PlayerSettings
    @ObservedObject var match: MatchSettings
    private let poll = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    private var i: Int { controls.editing }
    private var player: ControlsSettings.Player { controls.players[i] }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            PlayerSwitch(selected: controls.editing) { controls.editing = $0 }

            if players.players[i].cpu {
                MParagraph("P\(i + 1) is a CPU right now. These apply when P\(i + 1) plays as Human.",
                           size: 15, width: 330)
                    .foregroundStyle(.secondary)
            }

            Section(title: "Controller") {
                PortPicker(controls: controls, player: i)
            }

            Section(title: "Jump") {
                Toggle(isOn: Binding(get: { player.tapJump },
                                     set: { controls.setTapJump(i, $0) })) {
                    OptionLabel(symbol: "arrow.up.circle", text: "Tap jump")
                }
                Toggle(isOn: Binding(get: { player.zJump },
                                     set: { controls.setZJump(i, $0) })) {
                    OptionLabel(symbol: "arrow.up.to.line", text: "Z = jump")
                }
            }
            .toggleStyle(.switch)

            Section(title: "Buttons") {
                ButtonMapGrid(controls: controls, player: i)
            }

            Section(title: "All players") {
                Toggle(isOn: Binding(get: { controls.ucf }, set: { controls.setUCF($0) })) {
                    OptionLabel(symbol: "dial.medium", text: "UCF")
                }
                .toggleStyle(.switch)
                .disabled(controls.ucfLocked)
                .frame(maxWidth: .infinity, alignment: .leading)
                MParagraph(controls.ucfLocked
                           ? "Set by MELEE_UCF in the environment."
                           : "Fixes dashback and shield drops on any controller.",
                           size: 15, width: 300)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            MParagraph(match.active
                       ? "Buttons and jump apply now. The port applies at the next match."
                       : "Buttons and jump apply right away.",
                       size: 15, width: 330)
                .foregroundStyle(.tertiary)
        }
        .onAppear {
            controls.refresh()
            match.refresh()
        }
        .onReceive(poll) { _ in
            controls.refresh()
            match.refresh()
        }
    }
}

/// P1 / P2, each in its player color.
struct PlayerSwitch: View {
    let selected: Int
    let select: (Int) -> Void

    var body: some View {
        HStack(spacing: 0) {
            ForEach(0..<2, id: \.self) { i in
                Button {
                    if selected != i {
                        MenuSound.move.play()
                        select(i)
                    }
                } label: {
                    MText("P\(i + 1)", size: 20)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 4)
                        .background(Capsule().fill(selected == i ? PlayerColor.of(i) : Color.clear))
                        .foregroundStyle(selected == i ? Color.white : Color.secondary)
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(2)
        .background(Capsule().fill(Color.primary.opacity(0.08)))
    }
}

// MARK: - Port

/// Ports 1-4 with what is plugged into each. Port 1 also has the keyboard.
struct PortPicker: View {
    @ObservedObject var controls: ControlsSettings
    let player: Int

    var body: some View {
        let mine = controls.players[player].port
        let other = controls.players[1 - player].port
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                ForEach(0..<4, id: \.self) { port in
                    Button {
                        controls.setPort(player, port)
                    } label: {
                        VStack(spacing: 2) {
                            Image(systemName: icon(port))
                                .font(.system(size: 13, weight: .semibold))
                                .frame(height: 15)
                            MText("\(port + 1)", size: 18)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 5)
                        .background(
                            RoundedRectangle(cornerRadius: 8, style: .continuous)
                                .fill(port == mine ? PlayerColor.of(player)
                                                   : Color.primary.opacity(0.06))
                        )
                        .overlay(alignment: .topTrailing) {
                            if port == other {
                                Text("P\(2 - player)")
                                    .font(.system(size: 7, weight: .heavy))
                                    .foregroundStyle(.white)
                                    .padding(.horizontal, 2)
                                    .background(RoundedRectangle(cornerRadius: 2)
                                        .fill(PlayerColor.of(1 - player)))
                                    .padding(3)
                            }
                        }
                        .foregroundStyle(port == mine ? Color.white
                                         : controls.devices[port].isEmpty && port != 0
                                         ? Color.secondary : Color.primary)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            HStack(spacing: 6) {
                Image(systemName: icon(mine))
                    .frame(width: 18)
                    .foregroundStyle(.secondary)
                MText(description(mine), size: fittedSize(description(mine), size: 17, width: 270))
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func description(_ port: Int) -> String {
        let device = controls.devices[port]
        if port == 0 {
            return device.isEmpty ? "Port 1: keyboard" : "Port 1: \(device) + keyboard"
        }
        return device.isEmpty ? "Port \(port + 1): nothing plugged in" : "Port \(port + 1): \(device)"
    }

    private func icon(_ port: Int) -> String {
        if !controls.devices[port].isEmpty { return "gamecontroller.fill" }
        return port == 0 ? "keyboard" : "circle.dashed"
    }
}

// MARK: - Button map

/// GameCube face colors for the button badges.
enum PadColor {
    static func of(_ button: Int) -> Color {
        switch button {
        case 0: return Color(red: 0.10, green: 0.70, blue: 0.55) // A
        case 1: return Color(red: 0.86, green: 0.22, blue: 0.24) // B
        case 4: return Color(red: 0.42, green: 0.32, blue: 0.85) // Z
        case ControlsSettings.none: return Color.primary.opacity(0.15)
        default: return Color(white: 0.55) // X Y L R
        }
    }
}

struct PadBadge: View {
    let button: Int

    var body: some View {
        let name = button < ControlsSettings.buttons.count ? ControlsSettings.buttons[button] : "-"
        MText(name, size: 17)
            .foregroundStyle(.white)
            .frame(width: 24, height: 22)
            .background(Capsule().fill(PadColor.of(button)))
    }
}

/// Each physical button and the Melee button it acts as, two columns.
struct ButtonMapGrid: View {
    @ObservedObject var controls: ControlsSettings
    let player: Int

    var body: some View {
        let map = controls.players[player].map
        let columns = Array(repeating: GridItem(.flexible(), spacing: 6, alignment: .leading), count: 3)
        LazyVGrid(columns: columns, alignment: .leading, spacing: 8) {
            ForEach(0..<ControlsSettings.buttons.count, id: \.self) { b in
                ButtonMapRow(button: b, target: map[b]) { t in
                    controls.setMap(player, button: b, target: t)
                }
            }
            Button { controls.reset(player) } label: { MText("Reset", size: 16) }
                .disabled(map == Array(0..<ControlsSettings.buttons.count))
        }
    }
}

/// "A -> [A]": tapping the target lists the buttons to pick from.
struct ButtonMapRow: View {
    let button: Int
    let target: Int
    let pick: (Int) -> Void
    @State private var open = false

    var body: some View {
        HStack(spacing: 6) {
            PadBadge(button: button)
            Image(systemName: "arrow.right")
                .font(.system(size: 10, weight: .bold))
                .foregroundStyle(.tertiary)
            Button { open.toggle() } label: {
                HStack(spacing: 4) {
                    PadBadge(button: target)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 4)
                .padding(.vertical, 2)
                .background(RoundedRectangle(cornerRadius: 6).fill(Color.primary.opacity(
                    target == button ? 0 : 0.1)))
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .popover(isPresented: $open, arrowEdge: .bottom) {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(0...ControlsSettings.none, id: \.self) { t in
                        Button {
                            open = false
                            pick(t)
                        } label: {
                            HStack(spacing: 8) {
                                PadBadge(button: t)
                                MText(t == ControlsSettings.none ? "Nothing" : label(t), size: 16)
                                Spacer(minLength: 0)
                                if t == target {
                                    Image(systemName: "checkmark")
                                        .font(.system(size: 10, weight: .bold))
                                }
                            }
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(8)
                .frame(width: 150)
            }
        }
    }

    private func label(_ t: Int) -> String {
        switch t {
        case 0: return "A"
        case 1: return "B"
        case 2: return "X (jump)"
        case 3: return "Y (jump)"
        case 4: return "Z (grab)"
        case 5: return "L (shield)"
        default: return "R (shield)"
        }
    }
}
