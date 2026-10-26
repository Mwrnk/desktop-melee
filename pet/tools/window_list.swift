// SPDX-License-Identifier: GPL-3.0-or-later
// window_list: prints every on-screen window as "<owner>\t<layer>\t<x> <y> <w> <h>"
// (no Screen Recording needed for bounds). Used by pet/tests/welcome_window_test.sh.
import CoreGraphics
import Foundation
let opts: CGWindowListOption = [.optionOnScreenOnly]
let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as! [[String: Any]]
for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? "?"
    let layer = w[kCGWindowLayer as String] as? Int ?? 0
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let alpha = w[kCGWindowAlpha as String] as? Double ?? 1
    _ = alpha
    print("\(owner)\t\(layer)\t\(b["X"] ?? 0) \(b["Y"] ?? 0) \(b["Width"] ?? 0) \(b["Height"] ?? 0)")
}
