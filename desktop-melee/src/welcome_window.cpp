/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's welcome window (welcome_window.h), following
 * docs/desktop-melee/ui-guide.md sections 9 to 11, in the macOS app's order
 * and look (desktop-melee/ui/Welcome.swift, WelcomeSteps.swift):
 *
 *   1 Your disc    drop or choose it; the check runs       before the game boots
 *   - Loading      "Getting Melee ready...", the pipelines first run, while the
 *                                                          launcher builds them
 *   2 Welcome      what Desktop Melee is                   first run, once
 *                                                          assets load
 *   3 Controllers  press a button on each one
 *   4 You're set   start a match                           (macOS's step 4,
 *                                                          Accessibility, is
 *                                                          skipped here)
 *
 * Step 1 blocks desktop_melee_welcome_choose_disc, which runs aurora's event
 * and frame loop itself (the game isn't running yet), as melee-pc's launcher
 * does. From the loading step on, the game's own frames draw it. */
#include "welcome_window.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/rmlui.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "rml_dashed.h"
#include "window_icon.h"

extern "C" {
#include "../ui/bridge/desktop_melee_bridge.h"
#include "settings_file.h"
#include "settings_window.h"
extern bool pc_exit_requested; /* src/pc/pc.h */
}

namespace {

enum class Step { Disc, Loading, Welcome, Controls, Done };

/* desktop_melee_sound_menu kinds (ui-guide.md section 1). */
enum { SOUND_BACK = 0, SOUND_CONFIRM = 1, SOUND_MOVE = 2, SOUND_DENIED = 3 };

constexpr const char* kContextName = "desktop-melee-welcome";
/* Later runs show the loading window only when this much is left to build:
 * a few seconds of work (ui-guide.md section 11). */
constexpr unsigned kLoadingWindowThreshold = 500;

Rml::ElementDocument* s_document = nullptr;
Step s_step = Step::Disc;
bool s_first_run = false;    /* the whole flow, steps 1 to 4 */
bool s_loading_only = false; /* later runs: just the loading window */
bool s_choosing = false;     /* inside desktop_melee_welcome_choose_disc */
bool s_quit = false;         /* closed, or the game was asked to quit */
bool s_continue = false;     /* Continue pressed with a verified disc */
bool s_assets_ready = false;

/* The disc step. */
std::string s_disc;     /* the image shown, checked or checking */
std::string s_verified; /* the image that passed */
DesktopMeleeDiscCheck* s_check = nullptr;
std::string s_result;       /* returned by choose_disc */
std::mutex s_pending_mutex; /* the file dialog may answer on another thread */
std::string s_pending_disc;

/* The loading progress. */
bool s_loading = false;
unsigned s_loading_total = 0;
Uint64 s_loading_since = 0;
Uint64 s_last_loading_refresh = 0;

/* The controller step. */
struct Press {
    int port;
    std::string device;
};
std::vector<Press> s_pressed;
uint32_t s_last_buttons[4] = {};
bool s_keys_shown = false;

DesktopMeleeSettings& settings() {
    return *desktop_melee_settings();
}

std::string resources_path() {
    const char* base = SDL_GetBasePath();
    return std::string(base != nullptr ? base : "./") + "resources/";
}

void play_sound(int kind) {
    if (s_assets_ready && settings().menu_sounds) {
        desktop_melee_sound_menu(kind);
    }
}

/* Start on a controller must not start a match underneath the window while
 * it listens for presses; the player's own setting applies again after. */
void hold_start(bool held) {
    desktop_melee_set_start_starts_match(!held && settings().start_on_controller);
}

/* ---- elements ----------------------------------------------------------- */

Rml::Element* element(const std::string& id) {
    return s_document != nullptr ? s_document->GetElementById(id) : nullptr;
}

void set_text(const std::string& id, const std::string& text) {
    if (Rml::Element* e = element(id)) {
        e->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    }
}

void set_class(const std::string& id, const char* name, bool on) {
    if (Rml::Element* e = element(id)) {
        e->SetClass(name, on);
    }
}

void set_attribute(const std::string& id, const char* name, const std::string& value) {
    if (Rml::Element* e = element(id)) {
        if (e->GetAttribute<Rml::String>(name, "") != value) {
            e->SetAttribute(name, value);
        }
    }
}

void set_fill(const std::string& id, float fraction) {
    if (Rml::Element* e = element(id)) {
        const int percent = static_cast<int>(std::clamp(fraction, 0.f, 1.f) * 100.f + 0.5f);
        e->SetProperty("width", std::to_string(percent) + "%");
    }
}

std::string clicked_id(Rml::Element* e) {
    for (; e != nullptr; e = e->GetParentNode()) {
        if (!e->GetId().empty()) {
            return e->GetId();
        }
    }
    return {};
}

std::string file_name(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/* macOS's refusal: a quick shake of the status card (WelcomeMotion.swift). */
void shake(const std::string& id) {
    Rml::Element* e = element(id);
    if (e == nullptr) {
        return;
    }
    const Rml::Property rest{0.f, Rml::Unit::DP};
    const Rml::Tween tween{Rml::Tween::Sine, Rml::Tween::InOut};
    e->Animate("left", Rml::Property{8.f, Rml::Unit::DP}, 0.05f, tween, 1, false, 0.f, &rest);
    e->AddAnimationKey("left", Rml::Property{-7.f, Rml::Unit::DP}, 0.09f, tween);
    e->AddAnimationKey("left", Rml::Property{5.f, Rml::Unit::DP}, 0.08f, tween);
    e->AddAnimationKey("left", Rml::Property{-3.f, Rml::Unit::DP}, 0.07f, tween);
    e->AddAnimationKey("left", rest, 0.06f, tween);
}

/* ---- the Smash emblem --------------------------------------------------- */

/* The tray icon's drawing until the emblem is there, as macOS shows the
 * app's icon. */
void refresh_emblem() {
    if (!s_assets_ready || s_document == nullptr) {
        return;
    }
    set_attribute("emblem-img", "src", "dmemblem://emblem");
    set_attribute("tray-emblem-img", "src", "dmemblem://emblem");
    set_class("emblem-img", "hidden", false);
    set_class("tray-emblem-img", "hidden", false);
    s_document->SetClass("ready", true);
}

/* ---- the disc ----------------------------------------------------------- */

bool path_info(const std::string& path, long long* size, long long* mtime) {
    SDL_PathInfo info;
    if (path.empty() || !SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
        return false;
    }
    *size = static_cast<long long>(info.size);
    *mtime = static_cast<long long>(info.modify_time / 1000000000); /* ns to s */
    return true;
}

/* The same file that passed before: path, size and modification time
 * (ui-guide.md section 9; the hash runs once per file). */
bool remembered(const std::string& path) {
    long long size = 0;
    long long mtime = 0;
    const DesktopMeleeSettings& s = settings();
    return s.disc_path[0] != '\0' && path == s.disc_path && path_info(path, &size, &mtime) &&
           size == s.disc_size && mtime == s.disc_mtime;
}

void remember(const std::string& path) {
    long long size = 0;
    long long mtime = 0;
    DesktopMeleeSettings& s = settings();
    if (path_info(path, &size, &mtime)) {
        snprintf(s.disc_path, sizeof s.disc_path, "%s", path.c_str());
        s.disc_size = size;
        s.disc_mtime = mtime;
    }
    s.onboarding_done = true;
    desktop_melee_settings_save();
}

void stop_check() {
    if (s_check != nullptr) {
        desktop_melee_disc_check_free(s_check);
        s_check = nullptr;
    }
}

void refresh_nav();

enum class DiscState { Checking, Verified, Failed };

void show_disc_status(const std::string& line, DiscState state) {
    static const char* const icons[] = {
        "icons/opticaldisc.png", "icons/checkmark.circle.fill.png", "icons/xmark.octagon.fill.png"};
    set_class("disc-status", "hidden", false);
    set_text("disc-line", line);
    set_class("disc-status", "checking", state == DiscState::Checking);
    set_class("disc-status", "ok", state == DiscState::Verified);
    set_class("disc-status", "bad", state == DiscState::Failed);
    set_class("disc-bar", "hidden", state != DiscState::Checking);
    set_attribute("disc-icon", "src", icons[static_cast<int>(state)]);
    set_text("disc-name", file_name(s_disc));
}

void start_check(const std::string& path) {
    stop_check();
    s_disc = path;
    s_verified.clear();
    s_check = desktop_melee_disc_check_start(path.c_str());
    set_fill("disc-fill", 0.f);
    show_disc_status("Checking your disc... 0%", DiscState::Checking);
    set_text("choose", "Choose file...");
    refresh_nav();
}

void poll_check() {
    if (s_check == nullptr) {
        return;
    }
    int percent = 0;
    char message[256] = "";
    const DesktopMeleeDiscState state =
        desktop_melee_disc_check_poll(s_check, &percent, message, (int)sizeof message);
    if (state == DESKTOP_MELEE_DISC_CHECKING) {
        set_fill("disc-fill", percent / 100.f);
        set_text("disc-line", "Checking your disc... " + std::to_string(percent) + "%");
        return;
    }
    stop_check();
    if (state == DESKTOP_MELEE_DISC_VERIFIED) {
        s_verified = s_disc;
        show_disc_status("Super Smash Bros. Melee \xC2\xB7 USA \xC2\xB7 1.02 \xC2\xB7 verified",
            DiscState::Verified);
    } else {
        /* The check's own message: the PAL disc, a modified or bad copy, not
         * a disc image. */
        show_disc_status(message[0] != '\0' ? message : "This isn't a disc Desktop Melee can use.",
            DiscState::Failed);
        set_text("choose", "Choose another...");
        shake("disc-status");
        play_sound(SOUND_DENIED);
    }
    refresh_nav();
}

void SDLCALL on_file_chosen(void*, const char* const* files, int) {
    if (files != nullptr && files[0] != nullptr) {
        std::lock_guard lock{s_pending_mutex};
        s_pending_disc = files[0];
    }
}

void choose_file() {
    static const SDL_DialogFileFilter filters[] = {{"Melee disc image", "iso;gcm;ciso;rvz"}};
    const char* downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    SDL_ShowOpenFileDialog(on_file_chosen, nullptr, nullptr, filters, 1, downloads, false);
}

void on_drop(const char* path, void*) {
    if (s_step == Step::Disc && path != nullptr) {
        std::lock_guard lock{s_pending_mutex};
        s_pending_disc = path;
    }
}

void take_pending_disc() {
    std::string path;
    {
        std::lock_guard lock{s_pending_mutex};
        path.swap(s_pending_disc);
    }
    if (!path.empty() && s_step == Step::Disc) {
        play_sound(SOUND_CONFIRM);
        start_check(path);
    }
}

/* ---- the loading progress ----------------------------------------------- */

void refresh_loading() {
    const unsigned pending = desktop_melee_pipelines_pending();
    /* The queue is full from the start: the most seen pending is the total. */
    s_loading_total = std::max(s_loading_total, pending);
    const unsigned done = s_loading_total - pending;
    set_fill("loading-fill", s_loading_total > 0 ? float(done) / float(s_loading_total) : 1.f);
    if (!s_loading) {
        set_text("loading-count", "Starting the game...");
        return;
    }
    std::string count =
        "Graphics: " + std::to_string(done) + " of " + std::to_string(s_loading_total);
    const double seconds = (SDL_GetTicks() - s_loading_since) / 1000.0;
    if (done > 0 && seconds > 2.0 && pending > 0) {
        const int left = static_cast<int>(pending / (done / seconds) + 0.5);
        count += left >= 60 ? " \xC2\xB7 about " + std::to_string((left + 30) / 60) + " min left" :
                              " \xC2\xB7 about " + std::to_string(std::max(1, left)) + " s left";
    }
    set_text("loading-count", count);
}

/* ---- the controller step ------------------------------------------------ */

std::string port_device(int port) {
    const char* device = desktop_melee_port_device(port);
    std::string name = device != nullptr ? device : "";
    if (name.empty() && port == 0) {
        name = "Keyboard";
    }
    return name.empty() ? "Controller" : name;
}

/* "DualSense Wireless Controller" to "DualSense": the slot is small. */
std::string short_name(const std::string& name) {
    for (const char* suffix : {" Wireless Controller", " Controller", " controller"}) {
        const size_t n = std::strlen(suffix);
        if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) {
            return name.substr(0, name.size() - n);
        }
    }
    return name;
}

bool is_gamecube(const std::string& device) {
    return device.find("GameCube") != std::string::npos ||
           device.find("WaveBird") != std::string::npos;
}

std::string key(int slot) {
    const char* name = desktop_melee_key_name(slot);
    return name != nullptr && name[0] != '\0' ? name : "?";
}

std::string cap(const std::string& text) {
    return "<span class=\"cap\">" + Rml::StringUtilities::EncodeRml(text) + "</span>";
}

/* Read live from melee-pc's bindings (ui-guide.md section 9, step 3). Slots:
 * 0-3 A B X Y, 4 Start, 5-7 Z L R, 12-15 stick, 16-19 C-stick. */
void refresh_keys() {
    std::string rows;
    const struct {
        const char* button;
        int slot;
    } buttons[] = {
        {"A", 0}, {"B", 1}, {"X", 2}, {"Y", 3}, {"Z", 5}, {"L", 6}, {"R", 7}, {"Start", 4}};
    for (const auto& b : buttons) {
        rows += "<div class=\"key\"><span class=\"what\">" + std::string(b.button) + "</span>" +
                cap(key(b.slot)) + "</div>";
    }
    const bool arrows =
        key(14) == "Up" && key(15) == "Down" && key(13) == "Left" && key(12) == "Right";
    std::string stick;
    if (arrows) {
        stick = cap("Arrows");
    } else {
        stick = cap(key(14)) + cap(key(13)) + cap(key(15)) + cap(key(12));
    }
    rows += "<div class=\"key wide\"><span class=\"what\">Stick</span>" + stick + "</div>";
    rows += "<div class=\"key wide\"><span class=\"what\">C-stick</span>" + cap(key(18)) +
            cap(key(17)) + cap(key(19)) + cap(key(16)) + "</div>";
    if (Rml::Element* e = element("key-grid")) {
        e->SetInnerRML(rows);
    }
    set_class("step-controls", "keys", s_keys_shown);
    set_class("step-controls", "players", !s_keys_shown);
    set_class("keys-toggle", "open", s_keys_shown);
    set_text("keys-label", s_keys_shown ? "Hide keys" : "Show keys");
}

/* Four slots that fill as players press; the next free one is dashed in its
 * player's colour. A card lights up in the colour of the first player whose
 * press came from that kind of device. The slots are fixed elements, so only
 * the one that changes animates. */
void refresh_pressed() {
    const Press* by_port[4] = {};
    for (const Press& p : s_pressed) {
        by_port[p.port] = &p;
    }
    bool next_marked = false;
    for (int port = 0; port < 4; port++) {
        const std::string slot = "slot-" + std::to_string(port);
        const Press* p = by_port[port];
        if (p != nullptr) {
            set_text("who-" + std::to_string(port), short_name(p->device));
        }
        set_class(slot, "filled", p != nullptr);
        set_class(slot, "next", p == nullptr && !next_marked);
        next_marked = next_marked || p == nullptr;
    }
    set_text("listen-label", s_pressed.empty() ? "Press any button" : "Listening for more players");

    int lit[3] = {-1, -1, -1}; /* GameCube, other pad, keyboard: the port */
    for (const Press& p : s_pressed) {
        const int kind = p.device == "Keyboard" ? 2 : is_gamecube(p.device) ? 0 : 1;
        if (lit[kind] < 0) {
            lit[kind] = p.port;
        }
    }
    const char* cards[3] = {"card-gamecube", "card-pad", "card-keyboard"};
    for (int i = 0; i < 3; i++) {
        set_class(cards[i], "lit", lit[i] >= 0);
        for (int port = 0; port < 4; port++) {
            const std::string name = "lit-p" + std::to_string(port + 1);
            set_class(cards[i], name.c_str(), lit[i] == port);
        }
    }
}

/* desktop_melee_pad_buttons on every port: a newly pressed port joins the
 * list once (ui-guide.md polls 30 times a second; this runs every frame). */
void poll_pads() {
    bool changed = false;
    for (int port = 0; port < 4; port++) {
        const uint32_t buttons = desktop_melee_pad_buttons(port);
        const bool newly = buttons != 0 && s_last_buttons[port] == 0;
        s_last_buttons[port] = buttons;
        if (!newly) {
            continue;
        }
        const bool known = std::any_of(
            s_pressed.begin(), s_pressed.end(), [port](const Press& p) { return p.port == port; });
        if (!known) {
            s_pressed.push_back({port, port_device(port)});
            changed = true;
        }
    }
    if (changed) {
        play_sound(SOUND_CONFIRM);
        refresh_pressed();
    }
}

/* ---- steps -------------------------------------------------------------- */

const char* step_id(Step step) {
    switch (step) {
    case Step::Disc:
        return "step-disc";
    case Step::Loading:
        return "step-loading";
    case Step::Welcome:
        return "step-welcome";
    case Step::Controls:
        return "step-controls";
    case Step::Done:
        return "step-done";
    }
    return "";
}

/* One dot per step, the current one a red pill; macOS has 5 with its
 * Accessibility step. The loading screen stays on the disc's dot. */
void refresh_dots() {
    const int current = s_step == Step::Disc || s_step == Step::Loading ? 1 :
                        s_step == Step::Welcome                         ? 2 :
                        s_step == Step::Controls                        ? 3 :
                                                                          4;
    Rml::Element* dots = element("dots");
    if (dots == nullptr) {
        return;
    }
    dots->SetClass("hidden", s_loading_only);
    for (int i = 0; i < dots->GetNumChildren(); i++) {
        Rml::Element* dot = dots->GetChild(i);
        dot->SetClass("now", i + 1 == current);
        dot->SetClass("done", i + 1 < current);
    }
}

bool can_go_back() {
    return (s_step == Step::Controls && s_first_run) || s_step == Step::Done;
}

void refresh_nav() {
    const char* label = "";
    bool enabled = true;
    switch (s_step) {
    case Step::Disc:
        label = "Continue";
        enabled = !s_verified.empty() && s_check == nullptr;
        break;
    case Step::Loading:
        break;
    case Step::Welcome:
        label = "Get started";
        break;
    case Step::Controls:
        label = "Continue";
        break;
    case Step::Done:
        label = "Start a match";
        break;
    }
    set_class("nav", "hidden", s_step == Step::Loading);
    set_text("next", label);
    set_class("next", "disabled", !enabled);
    set_class("back", "hidden", !can_go_back());
    set_class("or-start", "hidden", !(s_step == Step::Done && settings().start_on_controller));
}

void show_step(Step step) {
    /* Going back slides the step in from the left (Step is in flow order). */
    set_class("steps", "backward", static_cast<int>(step) < static_cast<int>(s_step));
    s_step = step;
    const Step all[] = {Step::Disc, Step::Loading, Step::Welcome, Step::Controls, Step::Done};
    for (Step s : all) {
        set_class(step_id(s), "shown", s == step);
    }
    if (step == Step::Disc) {
        const bool again = !s_first_run;
        set_text("disc-title", again ? "Find your Melee disc again" : "Bring your own Melee");
        set_text("disc-lead",
            again ? "Desktop Melee can't use the disc it played last time. Choose your Melee "
                    "disc image again." :
                    "Desktop Melee includes the game's code but none of Nintendo's data. "
                    "Characters, stages and sound come from your own copy of Melee.");
        const std::string old = settings().disc_path;
        set_class("old-disc", "hidden", !again || old.empty());
        set_text("old-disc-path", old);
        set_class("disc-status", "hidden", s_disc.empty());
    }
    if (step == Step::Loading) {
        set_text("loading-lead", s_loading_only ?
                                     "Rebuilding graphics after an update. Only this once." :
                                     "The first launch builds Melee's graphics, so matches never "
                                     "stutter. It takes a few minutes, once. You can use your "
                                     "computer in the meantime.");
        refresh_loading();
    }
    if (step == Step::Controls) {
        hold_start(true);
        s_pressed.clear();
        s_keys_shown = false;
        for (int port = 0; port < 4; port++) {
            s_last_buttons[port] = desktop_melee_pad_buttons(port); /* only new presses count */
        }
        refresh_keys();
        refresh_pressed();
    }
    if (step == Step::Done) {
        hold_start(false);
    }
    refresh_emblem();
    refresh_dots();
    refresh_nav();
}

void close_window() {
    stop_check();
    if (s_document != nullptr) {
        s_document = nullptr;
        aurora::rmlui::close_window();
    }
    hold_start(false);
    s_loading_only = false;
}

void press_next() {
    switch (s_step) {
    case Step::Disc:
        if (s_verified.empty() || s_check != nullptr) {
            play_sound(SOUND_DENIED);
            return;
        }
        play_sound(SOUND_CONFIRM);
        s_continue = true;
        break;
    case Step::Loading:
        break;
    case Step::Welcome:
        play_sound(SOUND_CONFIRM);
        show_step(Step::Controls);
        break;
    case Step::Controls:
        play_sound(SOUND_CONFIRM);
        show_step(Step::Done);
        break;
    case Step::Done:
        play_sound(SOUND_CONFIRM);
        desktop_melee_request_match();
        close_window();
        break;
    }
}

void press_back() {
    if (!can_go_back()) {
        return;
    }
    play_sound(SOUND_BACK);
    show_step(s_step == Step::Done ? Step::Controls : Step::Welcome);
}

class Listener : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        const std::string id = clicked_id(event.GetTargetElement());
        if (id == "next") {
            press_next();
        } else if (id == "back") {
            press_back();
        } else if (id == "choose") {
            play_sound(SOUND_CONFIRM);
            choose_file();
        } else if (id == "card-keyboard" || id == "keys-toggle" || id == "keys-label" ||
                   id == "keys-chevron")
        {
            s_keys_shown = !s_keys_shown;
            play_sound(SOUND_MOVE);
            refresh_keys();
        }
    }
};
Listener s_listener;

void on_frame(void*) {
    if (s_document == nullptr) {
        return;
    }
    take_pending_disc();
    poll_check();
    if (s_step == Step::Loading && SDL_GetTicks() - s_last_loading_refresh >= 250) {
        s_last_loading_refresh = SDL_GetTicks();
        refresh_loading();
    }
    if (s_step == Step::Controls) {
        poll_pads();
    }
}

void on_closed(void*) {
    stop_check();
    s_document = nullptr;
    if (s_choosing) {
        s_quit = true; /* closing the window before the game boots quits it */
    }
    hold_start(false);
    s_loading_only = false;
}

bool open_window(Step step) {
    if (s_document != nullptr) {
        show_step(step);
        return true;
    }
    desktop_melee_settings_window_close(); /* one second window at a time */
    Rml::Context* context = aurora::rmlui::open_window(kContextName, "Desktop Melee", 440, 570);
    if (context == nullptr) {
        SDL_Log("desktop-melee: welcome window: could not open");
        return false;
    }
    const std::string resources = resources_path();
    Rml::LoadFontFace(resources + "menu.ttf", true);
    Rml::LoadFontFace(resources + "menu-bold.ttf");
    desktop_melee_rml_dashed_register();
    desktop_melee_set_window_icons();
    desktop_melee_emblem_texture_register();
    s_document = context->LoadDocument(resources + "desktop-melee/welcome.rml");
    if (s_document == nullptr) {
        SDL_Log("desktop-melee: welcome window: no %sdesktop-melee/welcome.rml", resources.c_str());
        aurora::rmlui::close_window();
        return false;
    }
    s_document->AddEventListener(Rml::EventId::Click, &s_listener);
    aurora::rmlui::set_window_closed_callback(on_closed, nullptr);
    aurora::rmlui::set_window_frame_callback(on_frame, nullptr);
    aurora::rmlui::set_window_drop_callback(on_drop, nullptr);
    show_step(step);
    s_document->Show();
    return true;
}

}  // namespace

/* dmemblem://emblem: the emblem the game decoded from the disc (an alpha
 * mask, desktop_melee_named_alpha), as white pixels to tint. Asked for only
 * once the assets are ready, so RmlUi never caches a missing image. */
void desktop_melee_emblem_texture_register(void) {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    aurora::rmlui::register_texture_provider(
        "dmemblem", [](std::string_view) -> std::optional<aurora::rmlui::RuntimeTexture> {
            static std::vector<std::byte> rgba;
            int w = 0;
            int h = 0;
            const uint8_t* alpha = desktop_melee_named_alpha("emblem", &w, &h);
            if (alpha == nullptr || w <= 0 || h <= 0) {
                return std::nullopt;
            }
            rgba.assign(static_cast<size_t>(w) * h * 4, std::byte{0xff});
            for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
                rgba[i * 4 + 3] = static_cast<std::byte>(alpha[i]);
            }
            return aurora::rmlui::RuntimeTexture{
                .width = static_cast<uint32_t>(w),
                .height = static_cast<uint32_t>(h),
                .rgba8 = rgba,
            };
        });
}

const char* desktop_melee_welcome_choose_disc(const char* candidate, const char* force) {
    const std::string disc = candidate != nullptr ? candidate : "";
    const bool force_disc = force != nullptr && SDL_strcmp(force, "disc") == 0;
    const bool force_all = force != nullptr && !force_disc;
    if (force == nullptr && settings().onboarding_done && remembered(disc)) {
        /* The disc that passed before: no welcome window. A small loading
         * window only when the graphics need rebuilding (a new build or a
         * driver update); melee-pc's launcher draws it from here on. */
        if (s_loading && desktop_melee_pipelines_pending() > kLoadingWindowThreshold) {
            s_first_run = false;
            s_loading_only = true;
            if (!open_window(Step::Loading)) {
                s_loading_only = false;
            }
        }
        return candidate;
    }
    s_first_run = force_all || (!force_disc && !settings().onboarding_done);
    s_quit = false;
    s_continue = false;
    s_verified.clear();
    s_disc.clear();
    if (!open_window(Step::Disc)) {
        return nullptr;
    }
    /* A remembered or command-line disc is checked as soon as the step
     * shows (ui-guide.md section 9). */
    long long size = 0;
    long long mtime = 0;
    if (path_info(disc, &size, &mtime)) {
        start_check(disc);
    }

    s_choosing = true;
    while (!s_quit && !s_continue && !pc_exit_requested) {
        for (const AuroraEvent* e = aurora_update(); e != nullptr && e->type != AURORA_NONE; ++e) {
            if (e->type == AURORA_EXIT) {
                s_quit = true; /* the game window was closed */
            }
        }
        if (aurora_begin_frame()) {
            aurora_end_frame();
        } else {
            SDL_Delay(16);
        }
    }
    s_choosing = false;

    if (!s_continue) {
        close_window();
        return nullptr;
    }
    s_result = s_verified;
    remember(s_result);
    if (s_first_run) {
        show_step(Step::Loading); /* stays open while the game gets ready */
    } else {
        close_window();
    }
    return s_result.c_str();
}

void desktop_melee_welcome_loading_begin(void) {
    s_loading = true;
    s_loading_total = 0;
    s_loading_since = SDL_GetTicks();
}

void desktop_melee_welcome_loading_end(void) {
    s_loading = false;
    if (s_loading_only) {
        close_window();
    } else if (s_document != nullptr && s_step == Step::Loading) {
        refresh_loading(); /* "Starting the game..." until the assets load */
    }
}

void desktop_melee_welcome_assets_ready(void) {
    s_assets_ready = true;
    if (s_document != nullptr && s_step == Step::Loading && !s_loading_only) {
        show_step(Step::Welcome);
    } else {
        refresh_emblem();
    }
}

void desktop_melee_welcome_how_to_play(void) {
    if (desktop_melee_welcome_busy()) {
        return;
    }
    s_loading_only = false;
    s_first_run = false; /* no Back to the welcome step */
    open_window(Step::Controls);
}

bool desktop_melee_welcome_busy(void) {
    return s_document != nullptr && (s_choosing || s_step == Step::Loading);
}

bool desktop_melee_welcome_is_open(void) {
    return s_document != nullptr;
}

void desktop_melee_welcome_close(void) {
    close_window();
}
