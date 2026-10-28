// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome window's motion: every duration and curve in one place, so the
// debug panel (DESKTOP_MELEE_WELCOME=debug) can slow it down and tune it.
// Reduce Motion keeps the fades and drops the movement.
import AppKit
import SwiftUI

final class WelcomeMotion: ObservableObject {
    static let shared = WelcomeMotion()

    struct Values: Equatable {
        /// The slide between steps: how long, and how far it travels.
        var stepDuration = 0.32
        var stepSlide = 28.0
        /// The spring for things that pop in (player slots, check marks).
        var popDuration = 0.45
        var popBounce = 0.32
        /// The delay between items that enter together.
        var stagger = 0.06
    }

    @Published var values = Values()
    /// Playback speed: below 1 is slow motion (debug only).
    @Published var speed = 1.0
    /// Debug: act as if Reduce Motion were on (DESKTOP_MELEE_REDUCE_MOTION=1
    /// from launch).
    @Published var forceReduce =
        ProcessInfo.processInfo.environment["DESKTOP_MELEE_REDUCE_MOTION"] == "1"

    var reduce: Bool {
        forceReduce || NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
    }

    /// A strong ease-out: starts fast, so it answers at once.
    func easeOut(_ duration: Double) -> Animation {
        Animation.timingCurve(0.23, 1, 0.32, 1, duration: duration).speed(speed)
    }

    /// Between steps.
    var step: Animation { easeOut(values.stepDuration) }
    /// Presses, hovers and colors.
    var quick: Animation { easeOut(0.18) }
    /// Things that arrive: a spring with a little bounce (a plain fade under
    /// Reduce Motion).
    var pop: Animation {
        reduce ? easeOut(0.2)
            : .spring(duration: values.popDuration, bounce: values.popBounce).speed(speed)
    }

    /// The values as text, for the debug panel's Copy.
    var summary: String {
        let v = values
        return String(format: "stepDuration %.2f, stepSlide %.0f, popDuration %.2f, "
                      + "popBounce %.2f, stagger %.3f", v.stepDuration, v.stepSlide,
                      v.popDuration, v.popBounce, v.stagger)
    }
}

extension EnvironmentValues {
    /// Offscreen renders (DESKTOP_MELEE_DUMP): draw every view at rest, since
    /// ImageRenderer never runs onAppear or a timeline.
    @Entry var welcomeStill = false
}

// MARK: - Transitions

/// Faded, scaled and moved by (x, y) when `active`. No blur: the window server
/// draws every filter, and one over a view that redraws every frame (the
/// looping sketches) stalled it, and with it the game's main thread.
struct FadeShift: ViewModifier {
    let active: Bool
    var x: CGFloat = 0
    var y: CGFloat = 0
    var scale: CGFloat = 1

    func body(content: Content) -> some View {
        content
            .opacity(active ? 0 : 1)
            .scaleEffect(active ? scale : 1)
            .offset(x: active ? x : 0, y: active ? y : 0)
    }
}

extension AnyTransition {
    /// A plain crossfade, as a modifier so it composes like the others.
    static var fade: AnyTransition {
        .modifier(active: FadeShift(active: true), identity: FadeShift(active: false))
    }

    /// Rises `y` points into place while fading in.
    static func rise(_ y: CGFloat) -> AnyTransition {
        .modifier(active: FadeShift(active: true, y: y), identity: FadeShift(active: false))
    }

    /// Grows from `scale` while fading in (never from nothing).
    static func pop(_ scale: CGFloat) -> AnyTransition {
        .modifier(active: FadeShift(active: true, scale: scale),
                  identity: FadeShift(active: false))
    }

    /// One step to the next: the new one slides in from the side it comes
    /// from; the old one only fades, a little faster, so the two never cross.
    static func welcomeStep(forward: Bool, motion: WelcomeMotion) -> AnyTransition {
        let slide = motion.reduce ? 0 : CGFloat(motion.values.stepSlide)
        let insertion = AnyTransition.modifier(
            active: FadeShift(active: true, x: forward ? slide : -slide),
            identity: FadeShift(active: false))
        let removal = AnyTransition.fade
            .animation(motion.easeOut(motion.values.stepDuration * 0.6))
        return .asymmetric(insertion: insertion, removal: removal)
    }
}

// MARK: - Entrance and loops

/// Fades and rises into place when it first appears, `index` stagger steps
/// after the first item. At rest in offscreen renders.
struct Entrance: ViewModifier {
    let index: Int
    @Environment(\.welcomeStill) private var still
    @State private var shown = false

    func body(content: Content) -> some View {
        let motion = WelcomeMotion.shared
        let visible = shown || still
        content
            .opacity(visible ? 1 : 0)
            .offset(y: visible || motion.reduce ? 0 : 10)
            .onAppear {
                withAnimation(motion.easeOut(0.5).delay(Double(index) * motion.values.stagger
                                                          / motion.speed)) {
                    shown = true
                }
            }
    }
}

extension View {
    func entrance(_ index: Int) -> some View { modifier(Entrance(index: index)) }
}

/// A motion that repeats for as long as its view is up.
enum Loop: Equatable {
    /// A full clockwise turn every `period` seconds.
    case spin(period: Double)
    /// Up `height` points and down again in the first half of each period,
    /// then a rest; `phase` (0-1) staggers several.
    case hop(height: Double, period: Double, phase: Double)
    /// Up and down by `distance`, smoothly.
    case bob(distance: Double, period: Double)
    /// Grows to `scale` while fading out, again and again: a ripple.
    case ripple(scale: Double, period: Double)
    /// Fades down to `low` and back.
    case breathe(low: Double, period: Double)
    /// Stretches sideways between `from` and `to` times its width.
    case stretch(from: Double, to: Double, period: Double)

    /// `up`: +1 where positive y is up in the layer, else -1.
    func animation(up: Double) -> CAAnimation {
        let animation: CAAnimation
        switch self {
        case .spin(let period):
            let a = CABasicAnimation(keyPath: "transform.rotation.z")
            a.fromValue = 0
            a.toValue = -2 * Double.pi * up
            a.duration = period
            animation = a
        case .hop(let height, let period, let phase):
            let a = CAKeyframeAnimation(keyPath: "transform.translation.y")
            a.values = [0, height * up, 0, 0]
            a.keyTimes = [0, 0.25, 0.5, 1]
            a.timingFunctions = [CAMediaTimingFunction(name: .easeOut),
                                 CAMediaTimingFunction(name: .easeIn),
                                 CAMediaTimingFunction(name: .linear)]
            a.duration = period
            a.timeOffset = phase * period
            animation = a
        case .bob(let distance, let period):
            let a = CABasicAnimation(keyPath: "transform.translation.y")
            a.fromValue = -distance * up
            a.toValue = distance * up
            a.autoreverses = true
            a.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            a.duration = period / 2
            animation = a
        case .ripple(let scale, let period):
            let grow = CABasicAnimation(keyPath: "transform.scale")
            grow.fromValue = 1
            grow.toValue = scale
            let fade = CABasicAnimation(keyPath: "opacity")
            fade.fromValue = 0.8
            fade.toValue = 0
            let group = CAAnimationGroup()
            group.animations = [grow, fade]
            group.timingFunction = CAMediaTimingFunction(name: .easeOut)
            group.duration = period
            animation = group
        case .breathe(let low, let period):
            let a = CABasicAnimation(keyPath: "opacity")
            a.fromValue = 1
            a.toValue = low
            a.autoreverses = true
            a.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            a.duration = period / 2
            animation = a
        case .stretch(let from, let to, let period):
            let a = CABasicAnimation(keyPath: "transform.scale.x")
            a.fromValue = from
            a.toValue = to
            a.autoreverses = true
            a.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            a.duration = period / 2
            animation = a
        }
        animation.repeatCount = .infinity
        animation.isRemovedOnCompletion = false
        return animation
    }
}

/// `content`, looping. Core Animation plays the loop in the window server, so
/// it costs nothing per frame on the main thread, which is the game's: a
/// SwiftUI loop (a TimelineView, a repeating animation) lays the window out
/// and commits it every frame, and stalled the game. The content is drawn
/// once into a layer, so it must not change while it loops. Still, as plain
/// SwiftUI, under Reduce Motion and in offscreen renders.
struct Looping<Content: View>: View {
    let loop: Loop
    let content: Content
    @ObservedObject private var motion = WelcomeMotion.shared
    @Environment(\.welcomeStill) private var still
    @Environment(\.font) private var font

    init(_ loop: Loop, @ViewBuilder content: () -> Content) {
        self.loop = loop
        self.content = content()
    }

    var body: some View {
        if still || motion.reduce {
            content
        } else {
            // The content keeps its place in the layout, unseen; the layer
            // over it draws it.
            content.hidden().overlay {
                LoopLayer(loop: loop, speed: motion.speed,
                          content: AnyView(content.font(font)
                              .environment(\.colorScheme, .dark)))
            }
        }
    }
}

private struct LoopLayer: NSViewRepresentable {
    let loop: Loop
    let speed: Double
    let content: AnyView

    func makeNSView(context: Context) -> LoopLayerView { LoopLayerView() }

    func updateNSView(_ view: LoopLayerView, context: Context) {
        view.update(loop: loop, speed: speed, content: content)
    }
}

/// Holds the looping layer: one it owns (AppKit resets a view's own layer on
/// layout), centered so it turns and scales about its middle. Lets clicks
/// through.
final class LoopLayerView: NSView {
    private let looped = CALayer()
    private var loop: Loop?
    private var content: AnyView?
    private var drawnSize = CGSize.zero
    private var drawnScale: CGFloat = 0

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        clipsToBounds = false
        layer?.addSublayer(looped)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    func update(loop: Loop, speed: Double, content: AnyView) {
        // Drawn again only when the size changes (layout): drawing on every
        // update fed back into SwiftUI and laid the window out every frame.
        self.content = content
        if looped.speed != Float(speed) { looped.speed = Float(speed) }
        if loop != self.loop {
            self.loop = loop
            restart()
        }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        drawnSize = .zero
        needsLayout = true
        restart()
    }

    private func restart() {
        looped.removeAllAnimations()
        guard let loop, window != nil else { return }
        // Which way is up in this layer.
        let up: Double = looped.contentsAreFlipped() ? -1 : 1
        looped.add(loop.animation(up: up), forKey: "loop")
    }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        looped.bounds = CGRect(origin: .zero, size: bounds.size)
        looped.position = CGPoint(x: bounds.midX, y: bounds.midY)
        let scale = window?.backingScaleFactor ?? 2
        if let content, bounds.size != drawnSize || scale != drawnScale,
           bounds.width > 0, bounds.height > 0 {
            drawnSize = bounds.size
            drawnScale = scale
            let renderer = ImageRenderer(content: content
                .frame(width: bounds.width, height: bounds.height))
            renderer.scale = scale
            looped.contents = renderer.cgImage
            looped.contentsScale = scale
        }
        CATransaction.commit()
    }
}

/// A view that spins once every `period` seconds.
struct Spinning<Content: View>: View {
    var period = 1.4
    @ViewBuilder let content: Content

    var body: some View {
        Looping(.spin(period: period)) { content }
    }
}

/// Shakes sideways once each time `trigger` changes, like a refused password.
struct Shake: ViewModifier {
    let trigger: Int

    func body(content: Content) -> some View {
        let motion = WelcomeMotion.shared
        let d = 0.07 / motion.speed
        content.keyframeAnimator(initialValue: 0.0, trigger: trigger) { view, x in
            view.offset(x: motion.reduce ? 0 : x)
        } keyframes: { _ in
            KeyframeTrack {
                LinearKeyframe(-7, duration: d)
                LinearKeyframe(6, duration: d)
                LinearKeyframe(-4, duration: d)
                LinearKeyframe(2, duration: d)
                SpringKeyframe(0, duration: d * 3)
            }
        }
    }
}

/// Pressable: shrinks a little while held, so a click feels heard.
struct PressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        let motion = WelcomeMotion.shared
        configuration.label
            .scaleEffect(configuration.isPressed && !motion.reduce ? 0.97 : 1)
            .animation(motion.easeOut(0.12), value: configuration.isPressed)
    }
}

/// Grows from `from` with the pop spring when it first appears.
struct PopOnAppear: ViewModifier {
    var from: CGFloat = 0.5
    @Environment(\.welcomeStill) private var still
    @State private var shown = false

    func body(content: Content) -> some View {
        let motion = WelcomeMotion.shared
        content
            .scaleEffect(shown || still || motion.reduce ? 1 : from)
            .onAppear { withAnimation(motion.pop) { shown = true } }
    }
}

/// Shakes once when it first appears.
struct ShakeOnAppear: ViewModifier {
    @Environment(\.welcomeStill) private var still
    @State private var count = 0

    func body(content: Content) -> some View {
        content
            .modifier(Shake(trigger: count))
            .onAppear { if !still { count += 1 } }
    }
}
