import Foundation

/// Where Desktop Melee keeps its settings: the `desktop-melee` defaults suite.
enum Settings {
    static let suiteName = "desktop-melee"

    /// Earlier builds saved to the `melee-pet` suite. The first time the new
    /// suite is used and is still empty, every key is copied over once; the
    /// old suite is left as it was.
    static let defaults: UserDefaults = {
        guard let suite = UserDefaults(suiteName: suiteName) else { return .standard }
        if suite.persistentDomain(forName: suiteName)?.isEmpty ?? true,
           let old = UserDefaults.standard.persistentDomain(forName: "melee-pet"), !old.isEmpty {
            for (key, value) in old { suite.set(value, forKey: key) }
            NSLog("desktop-melee: moved %d settings from melee-pet to %@", old.count, suiteName)
        }
        return suite
    }()
}
