// SPDX-License-Identifier: GPL-3.0-or-later
// The Players tab: character, costume and Human/CPU for P1 and P2, picked
// from Melee's own select grid.
import PetBridge
import SwiftUI

// MARK: - Roster

/// A character in the grid. Melee's select screen has no Sheik tile (Zelda
/// turns into Sheik), so Sheik borrows Zelda's icon and portraits.
struct Fighter: Identifiable {
    let ckind: Int
    let name: String
    /// The select grid entry whose face icon this tile shows.
    let rosterIndex: Int
    var id: Int { ckind }
    var isSheik: Bool { ckind == Roster.sheik }
}

enum Roster {
    static let zelda = 0x12
    static let sheik = 0x13
    /// Character kinds 0x00...0x19 are playable.
    static let playable = 0..<0x1A

    static let names: [Int: String] = [
        0x00: "Captain Falcon", 0x01: "Donkey Kong", 0x02: "Fox", 0x03: "Mr. Game & Watch",
        0x04: "Kirby", 0x05: "Bowser", 0x06: "Link", 0x07: "Luigi", 0x08: "Mario",
        0x09: "Marth", 0x0A: "Mewtwo", 0x0B: "Ness", 0x0C: "Peach", 0x0D: "Pikachu",
        0x0E: "Ice Climbers", 0x0F: "Jigglypuff", 0x10: "Samus", 0x11: "Yoshi",
        0x12: "Zelda", 0x13: "Sheik", 0x14: "Falco", 0x15: "Young Link",
        0x16: "Dr. Mario", 0x17: "Roy", 0x18: "Pichu", 0x19: "Ganondorf",
    ]

    /// The grid in Melee's order (rows of 9, 9 and 7), Sheik closing the last
    /// row. Empty until the game has loaded the roster.
    static func rows() -> [[Fighter]] {
        var all: [Fighter] = []
        var zeldaIndex: Int?
        for i in 0..<Int(pet_roster_count()) {
            var ckind: Int32 = 0, costumes: Int32 = 0
            guard pet_roster_entry(Int32(i), &ckind, &costumes) else { continue }
            let ck = Int(ckind)
            all.append(Fighter(ckind: ck, name: names[ck] ?? "?", rosterIndex: i))
            if ck == zelda { zeldaIndex = i }
        }
        if let z = zeldaIndex {
            all.append(Fighter(ckind: sheik, name: "Sheik", rosterIndex: z))
        }
        guard all.count > 18 else { return all.isEmpty ? [] : [all] }
        return [Array(all[0..<9]), Array(all[9..<18]), Array(all[18...])]
    }

    /// How many costumes `ckind` has, or nil before the roster is loaded.
    static func costumes(_ ckind: Int) -> Int? {
        let n = Int(pet_costume_count(Int32(ckind)))
        return n > 0 ? n : nil
    }

    /// The select screen portrait. Sheik shows Zelda's, as Melee's does.
    static func portrait(ckind: Int, costume: Int) -> CGImage? {
        GameImages.portrait(ckind: ckind == sheik ? zelda : ckind, costume: costume)
    }

    /// The select grid entry for `ckind` (its announcer callout); none for Sheik.
    static func rosterIndex(_ ckind: Int) -> Int? {
        guard ckind != sheik else { return nil }
        for i in 0..<Int(pet_roster_count()) {
            var ck: Int32 = 0, n: Int32 = 0
            if pet_roster_entry(Int32(i), &ck, &n), Int(ck) == ckind { return i }
        }
        return nil
    }
}

enum PlayerColor {
    static let p1 = Color(red: 0.91, green: 0.23, blue: 0.22)
    static let p2 = Color(red: 0.24, green: 0.46, blue: 0.96)

    static func of(_ player: Int) -> Color { player == 0 ? p1 : p2 }
}

// MARK: - Settings

/// P1 and P2's picks, remembered across launches and handed to the game's
/// GM_PET mode whenever they change. The rules part of the setup is left as
/// the game has it (the Match tab owns it).
final class PlayerSettings: ObservableObject {
    struct Player: Equatable {
        var ckind: Int
        var costume: Int
        var cpu: Bool
        var level: Int
    }

    private static let defaults = UserDefaults(suiteName: "melee-pet") ?? .standard

    @Published private(set) var players: [Player]
    /// The player the grid picks for.
    @Published var editing = 0

    init() {
        var setup = PetMatchSetup()
        pet_match_setup(&setup)
        let game = [setup.players.0, setup.players.1]
        let d = Self.defaults
        players = (0..<2).map { i in
            let key = "players.p\(i + 1)."
            let g = game[i]
            var ckind = d.object(forKey: key + "ckind") as? Int ?? Int(g.ckind)
            if !Roster.playable.contains(ckind) { ckind = Int(g.ckind) }
            return Player(ckind: ckind,
                          costume: max(0, d.object(forKey: key + "costume") as? Int ?? Int(g.costume)),
                          cpu: d.object(forKey: key + "cpu") as? Bool ?? (g.cpu != 0),
                          level: max(1, min(9, d.object(forKey: key + "level") as? Int ?? Int(g.cpu_level))))
        }
        push()
    }

    /// The grid picks `fighter` for the player being edited.
    func pick(_ fighter: Fighter) {
        let i = editing
        players[i].ckind = fighter.ckind
        commit()
        MenuSound.confirm.play()
        if let index = Roster.rosterIndex(fighter.ckind) {
            MenuSound.announce(rosterIndex: index)
        }
    }

    func setCostume(_ player: Int, _ costume: Int) {
        guard !costumeTaken(player, costume) else {
            MenuSound.denied.play()
            return
        }
        players[player].costume = costume
        commit()
        MenuSound.confirm.play()
    }

    func setCPU(_ player: Int, _ cpu: Bool) {
        guard players[player].cpu != cpu else { return }
        players[player].cpu = cpu
        commit()
        MenuSound.confirm.play()
    }

    func setLevel(_ player: Int, _ level: Int) {
        let level = max(1, min(9, level))
        guard players[player].level != level else { return }
        players[player].level = level
        commit()
        MenuSound.move.play()
    }

    /// The other player already wears this costume of the same character.
    func costumeTaken(_ player: Int, _ costume: Int) -> Bool {
        let other = players[1 - player]
        return other.ckind == players[player].ckind && other.costume == costume
    }

    /// Re-checks costumes once the roster is loaded (counts are known then).
    func validate() {
        let before = players
        normalize()
        if players != before { commit() }
    }

    /// Costumes in range for each character, and two players on the same
    /// character in different costumes (Melee's select screen moves the
    /// later one to the next free costume). The edited player gives way.
    private func normalize() {
        for i in 0..<2 {
            if let n = Roster.costumes(players[i].ckind), players[i].costume >= n {
                players[i].costume = 0
            }
        }
        let mover = editing
        if costumeTaken(mover, players[mover].costume) {
            let n = Roster.costumes(players[mover].ckind) ?? players[mover].costume + 2
            for step in 1..<max(2, n) {
                let c = (players[mover].costume + step) % max(1, n)
                if !costumeTaken(mover, c) {
                    players[mover].costume = c
                    break
                }
            }
        }
    }

    private func commit() {
        normalize()
        let d = Self.defaults
        for (i, p) in players.enumerated() {
            let key = "players.p\(i + 1)."
            d.set(p.ckind, forKey: key + "ckind")
            d.set(p.costume, forKey: key + "costume")
            d.set(p.cpu, forKey: key + "cpu")
            d.set(p.level, forKey: key + "level")
        }
        push()
    }

    private func push() {
        var setup = PetMatchSetup()
        pet_match_setup(&setup)
        func game(_ p: Player) -> PetPlayerSetup {
            PetPlayerSetup(ckind: Int32(p.ckind), costume: Int32(p.costume),
                           cpu: p.cpu ? 1 : 0, cpu_level: Int32(p.level))
        }
        setup.players.0 = game(players[0])
        setup.players.1 = game(players[1])
        pet_set_match_setup(&setup)
    }
}

// MARK: - Tab

struct PlayersTab: View {
    @ObservedObject var players: PlayerSettings
    @ObservedObject var match: MatchSettings
    @ObservedObject private var assets = GameAssets.shared
    private let poll = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .top, spacing: 10) {
                PlayerCard(index: 0, players: players)
                PlayerCard(index: 1, players: players)
            }
            CharacterGrid(players: players)
            MParagraph(match.active
                       ? "Changes apply at the next match. Restart applies them now."
                       : "Changes apply when the match starts.",
                       size: 15, width: 330)
                .foregroundStyle(.tertiary)
        }
        .onAppear {
            players.validate()
            match.refresh()
        }
        .onChange(of: assets.ready) { _ in players.validate() }
        .onReceive(poll) { _ in match.refresh() }
    }
}

// MARK: - Player card

struct PlayerCard: View {
    let index: Int
    @ObservedObject var players: PlayerSettings

    private var player: PlayerSettings.Player { players.players[index] }
    private var color: Color { PlayerColor.of(index) }
    private var editing: Bool { players.editing == index }
    private var name: String { Roster.names[player.ckind] ?? "?" }

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                MText("P\(index + 1)", size: 22)
                    .foregroundStyle(color)
                Spacer()
                if editing {
                    Image(systemName: "hand.point.up.left.fill")
                        .font(.system(size: 11))
                        .foregroundStyle(color)
                }
            }
            portrait
            MText(name, size: fittedSize(name, size: 19, width: 136))
                .frame(height: 19)
            CostumeRow(index: index, players: players)
            ControllerToggle(index: index, players: players)
            levelRow
                .opacity(player.cpu ? 1 : 0)
                .disabled(!player.cpu)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color.primary.opacity(0.06))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(editing ? color : Color.primary.opacity(0.08),
                              lineWidth: editing ? 2 : 1)
        )
        .contentShape(Rectangle())
        .onTapGesture { select() }
    }

    private func select() {
        guard !editing else { return }
        players.editing = index
        MenuSound.move.play()
    }

    /// The portrait art ends in a hard cut on its right side (the select
    /// screen hides it behind its frame), so it sits flush right.
    private var portrait: some View {
        ZStack(alignment: .trailing) {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(LinearGradient(colors: [color.opacity(0.75), color.opacity(0.2)],
                                     startPoint: .top, endPoint: .bottom))
            if let image = Roster.portrait(ckind: player.ckind, costume: player.costume) {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.high)
                    .aspectRatio(contentMode: .fit)
                    .padding(.top, 4)
            } else {
                Image(systemName: "person.fill")
                    .font(.system(size: 36))
                    .foregroundStyle(.white.opacity(0.6))
                    .frame(maxWidth: .infinity)
            }
        }
        .frame(height: 124)
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private var levelRow: some View {
        HStack(spacing: 6) {
            MText("Level", size: 17)
                .foregroundStyle(.secondary)
            Spacer()
            Button { select(); players.setLevel(index, player.level - 1) } label: {
                Image(systemName: "minus")
            }
            .buttonStyle(.borderless)
            MText("\(player.level)", size: 19)
                .frame(minWidth: 16)
            Button { select(); players.setLevel(index, player.level + 1) } label: {
                Image(systemName: "plus")
            }
            .buttonStyle(.borderless)
        }
    }
}

/// Melee's font size that fits `text` into `width` points, at most `size`.
func fittedSize(_ text: String, size: CGFloat, width: CGFloat) -> CGFloat {
    let w = MeleeFont.width(text, size: size)
    return w > width ? floor(size * width / w) : size
}

/// One small portrait per costume; the other player's costume of the same
/// character is shown taken.
struct CostumeRow: View {
    let index: Int
    @ObservedObject var players: PlayerSettings

    var body: some View {
        let player = players.players[index]
        let count = Roster.costumes(player.ckind) ?? 1
        HStack(spacing: 4) {
            ForEach(0..<count, id: \.self) { c in
                let selected = c == player.costume
                let taken = players.costumeTaken(index, c)
                Button {
                    players.editing = index
                    if !selected { players.setCostume(index, c) }
                } label: {
                    ZStack {
                        if let image = Roster.portrait(ckind: player.ckind, costume: c) {
                            Image(decorative: image, scale: 1)
                                .resizable()
                                .interpolation(.high)
                                .aspectRatio(contentMode: .fill)
                                .frame(width: 20, height: 26, alignment: .top)
                        } else {
                            Color.primary.opacity(0.1)
                        }
                        if taken {
                            Image(systemName: "xmark")
                                .font(.system(size: 9, weight: .bold))
                                .foregroundStyle(.white)
                        }
                    }
                    .frame(width: 20, height: 26)
                    .background(Color.primary.opacity(0.08))
                    .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))
                    .overlay(
                        RoundedRectangle(cornerRadius: 4, style: .continuous)
                            .strokeBorder(selected ? PlayerColor.of(index) : Color.clear,
                                          lineWidth: 2)
                    )
                    .opacity(taken ? 0.35 : 1)
                }
                .buttonStyle(.plain)
                .help(taken ? "P\(2 - index) wears this costume" : "Costume \(c + 1)")
            }
        }
        .frame(height: 26)
    }
}

/// Human or CPU, as a two-part switch in the player's color.
struct ControllerToggle: View {
    let index: Int
    @ObservedObject var players: PlayerSettings

    var body: some View {
        let cpu = players.players[index].cpu
        HStack(spacing: 0) {
            part("Human", selected: !cpu) { players.setCPU(index, false) }
            part("CPU", selected: cpu) { players.setCPU(index, true) }
        }
        .padding(2)
        .background(Capsule().fill(Color.primary.opacity(0.08)))
    }

    private func part(_ title: String, selected: Bool, action: @escaping () -> Void) -> some View {
        Button {
            players.editing = index
            action()
        } label: {
            MText(title, size: 16)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 3)
                .background(Capsule().fill(selected ? PlayerColor.of(index) : Color.clear))
                .foregroundStyle(selected ? Color.white : Color.secondary)
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Character grid

struct CharacterGrid: View {
    @ObservedObject var players: PlayerSettings
    @ObservedObject private var assets = GameAssets.shared

    static let spacing: CGFloat = 3
    static let tileWidth: CGFloat = (336 - 8 * spacing) / 9

    var body: some View {
        let rows = assets.ready ? Roster.rows() : []
        VStack(spacing: Self.spacing) {
            if rows.isEmpty {
                MText("Loading characters...", size: 16)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, minHeight: 90)
            }
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                HStack(spacing: Self.spacing) {
                    ForEach(row) { fighter in
                        FighterTile(fighter: fighter, players: players)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity)
    }
}

struct FighterTile: View {
    let fighter: Fighter
    @ObservedObject var players: PlayerSettings
    @State private var hovering = false

    private let width = CharacterGrid.tileWidth
    private var height: CGFloat { width * 56 / 64 }

    var body: some View {
        let editColor = PlayerColor.of(players.editing)
        Button {
            players.pick(fighter)
        } label: {
            ZStack(alignment: .topLeading) {
                icon
                badges
            }
            .frame(width: width, height: height)
            .clipShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 3, style: .continuous)
                    .strokeBorder(editColor, lineWidth: hovering ? 2 : 0)
            )
            .scaleEffect(hovering ? 1.06 : 1)
            .animation(.easeOut(duration: 0.1), value: hovering)
        }
        .buttonStyle(.plain)
        .zIndex(hovering ? 1 : 0)
        .accessibilityLabel(fighter.name)
        .help(fighter.name)
        .onHover { inside in
            hovering = inside
            if inside { MenuSound.move.play() }
        }
    }

    @ViewBuilder private var icon: some View {
        ZStack(alignment: .bottom) {
            if let image = GameImages.icon(fighter.rosterIndex) {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.high)
                    .frame(width: width, height: height)
            } else {
                Color.primary.opacity(0.1)
            }
            if fighter.isSheik {
                // Zelda's face icon, its name strip relabeled.
                Rectangle()
                    .fill(Color.black)
                    .frame(height: height * 15 / 56)
                MText("SHEIK", size: height * 0.32)
                    .foregroundStyle(.white)
                    .padding(.bottom, height * 0.01)
            }
        }
    }

    /// P1 / P2 on the characters they have picked.
    private var badges: some View {
        HStack(spacing: 1) {
            ForEach(0..<2, id: \.self) { i in
                if players.players[i].ckind == fighter.ckind {
                    Text("P\(i + 1)")
                        .font(.system(size: 7, weight: .heavy))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 2)
                        .background(RoundedRectangle(cornerRadius: 2).fill(PlayerColor.of(i)))
                }
            }
        }
        .padding(1)
    }
}
