/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's welcome window (welcome_window.h), following
 * docs/desktop-melee/ui-guide.md sections 9 to 11:
 *
 *   1 Welcome      what Desktop Melee is                   first run only
 *   2 Your disc    drop or choose it; the check runs       before the game boots
 *   - Loading      "Getting Melee ready...", the pipelines first run, while the
 *                                                          launcher builds them
 *   3 Controllers  press a button on each one              once assets load
 *   4 You're set   start a match                           (macOS's step 4,
 *                                                          Accessibility, is
 *                                                          skipped here)
 *
 * Steps 1 and 2 block desktop_melee_welcome_choose_disc, which runs aurora's
 * event and frame loop itself (the game isn't running yet), as melee-pc's
 * launcher does. From the loading step on, the game's own frames draw it. */
#include "welcome_window.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/rmlui.hpp>

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "../ui/bridge/desktop_melee_bridge.h"
#include "settings_file.h"
#include "settings_window.h"
extern bool pc_exit_requested; /* src/pc/pc.h */
}

namespace {

enum class Step { Welcome, Disc, Loading, Controls, Done };

/* desktop_melee_sound_menu kinds (ui-guide.md section 1). */
enum { SOUND_BACK = 0, SOUND_CONFIRM = 1, SOUND_MOVE = 2, SOUND_DENIED = 3 };

constexpr const char* kContextName = "desktop-melee-welcome";
/* Later runs show the loading window only when this much is left to build:
 * a few seconds of work (ui-guide.md section 11). */
constexpr unsigned kLoadingWindowThreshold = 500;

Rml::ElementDocument* s_document = nullptr;
Step s_step = Step::Welcome;
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

void refresh_next();

void show_disc_status(const std::string& line, const char* state, bool bar) {
    set_text("disc-line", line);
    set_class("disc-status", "ok", std::string(state) == "ok");
    set_class("disc-status", "bad", std::string(state) == "bad");
    set_class("disc-bar", "hidden", !bar);
    set_text("disc-name", file_name(s_disc));
}

void start_check(const std::string& path) {
    stop_check();
    s_disc = path;
    s_verified.clear();
    s_check = desktop_melee_disc_check_start(path.c_str());
    set_fill("disc-fill", 0.f);
    show_disc_status("Checking your disc... 0%", "", true);
    set_text("choose", "Choose file...");
    refresh_next();
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
        show_disc_status(
            "Super Smash Bros. Melee \xC2\xB7 USA \xC2\xB7 1.02 \xC2\xB7 verified", "ok", false);
    } else {
        /* The check's own message: the PAL disc, a modified or bad copy, not
         * a disc image. */
        show_disc_status(message[0] != '\0' ? message : "This isn't a disc Desktop Melee can use.",
            "bad", false);
        set_text("choose", "Choose another...");
    }
    refresh_next();
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
        rows += "<div class=\"row\"><span class=\"what\">" + std::string(b.button) + "</span>" +
                cap(key(b.slot)) + "</div>";
    }
    const bool arrows =
        key(14) == "Up" && key(15) == "Down" && key(13) == "Left" && key(12) == "Right";
    const bool wasd = key(14) == "W" && key(15) == "S" && key(13) == "A" && key(12) == "D";
    std::string stick;
    if (arrows || wasd) {
        stick = arrows ? "Arrow keys" : "W A S D";
    } else {
        stick = cap(key(14)) + cap(key(13)) + cap(key(15)) + cap(key(12));
    }
    rows += "<div class=\"row\"><span class=\"what\">Stick</span>" +
            (arrows || wasd ? cap(stick) : stick) + "</div>";
    rows += "<div class=\"row\"><span class=\"what\">C-stick</span>" + cap(key(18)) + cap(key(17)) +
            cap(key(19)) + cap(key(16)) + "</div>";
    if (Rml::Element* e = element("keys")) {
        e->SetInnerRML(rows);
        e->SetClass("hidden", !s_keys_shown);
    }
    set_text("keys-toggle", s_keys_shown ? "Hide the keys" : "Show the keys");
}

void refresh_pressed() {
    std::string slots;
    bool gamecube = false;
    bool pad = false;
    bool keyboard = false;
    for (const Press& p : s_pressed) {
        const std::string n = std::to_string(p.port + 1);
        slots += "<div class=\"slot\"><span class=\"tag p" + n + "\">P" + n + "</span><span>" +
                 Rml::StringUtilities::EncodeRml(p.device) + "</span></div>";
        if (p.device == "Keyboard") {
            keyboard = true;
        } else if (is_gamecube(p.device)) {
            gamecube = true;
        } else {
            pad = true;
        }
    }
    if (s_pressed.size() < 4) {
        slots += std::string("<div class=\"slot empty\">") +
                 (s_pressed.empty() ? "Waiting for a press..." : "Listening for more players") +
                 "</div>";
    }
    if (Rml::Element* e = element("pressed")) {
        e->SetInnerRML(slots);
    }
    set_class("card-gamecube", "lit", gamecube);
    set_class("card-pad", "lit", pad);
    set_class("card-keyboard", "lit", keyboard);
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
    case Step::Welcome:
        return "step-welcome";
    case Step::Disc:
        return "step-disc";
    case Step::Loading:
        return "step-loading";
    case Step::Controls:
        return "step-controls";
    case Step::Done:
        return "step-done";
    }
    return "";
}

/* "N of 4" in the full flow; macOS counts 5 with its Accessibility step. */
void refresh_counter() {
    int n = 0;
    if (s_first_run || s_step == Step::Controls || s_step == Step::Done) {
        n = s_step == Step::Welcome  ? 1 :
            s_step == Step::Disc     ? 2 :
            s_step == Step::Controls ? 3 :
            s_step == Step::Done     ? 4 :
                                       0;
    }
    set_text("counter", n > 0 ? std::to_string(n) + " of 4" : "");
}

void refresh_next() {
    const char* label = "";
    bool enabled = true;
    bool shown = true;
    switch (s_step) {
    case Step::Welcome:
        label = "Get started";
        break;
    case Step::Disc:
        label = "Continue";
        enabled = !s_verified.empty() && s_check == nullptr;
        break;
    case Step::Loading:
        shown = false;
        break;
    case Step::Controls:
        label = "Continue";
        break;
    case Step::Done:
        label = "Start a match";
        break;
    }
    set_text("next", label);
    set_class("next", "disabled", !enabled);
    set_class("next", "hidden", !shown);
    set_class("or-start", "hidden", !(s_step == Step::Done && settings().start_on_controller));
}

void show_step(Step step) {
    s_step = step;
    const Step all[] = {Step::Welcome, Step::Disc, Step::Loading, Step::Controls, Step::Done};
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
        for (int port = 0; port < 4; port++) {
            s_last_buttons[port] = desktop_melee_pad_buttons(port); /* only new presses count */
        }
        refresh_keys();
        refresh_pressed();
    }
    if (step == Step::Done) {
        hold_start(false);
    }
    refresh_counter();
    refresh_next();
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
    case Step::Welcome:
        play_sound(SOUND_CONFIRM);
        show_step(Step::Disc);
        break;
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

class Listener : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        const std::string id = clicked_id(event.GetTargetElement());
        if (id == "next") {
            press_next();
        } else if (id == "choose") {
            play_sound(SOUND_CONFIRM);
            choose_file();
        } else if (id == "card-keyboard" || id == "keys-toggle") {
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
    Rml::Context* context = aurora::rmlui::open_window(kContextName, "Desktop Melee", 440, 560);
    if (context == nullptr) {
        SDL_Log("desktop-melee: welcome window: could not open");
        return false;
    }
    const std::string resources = resources_path();
    Rml::LoadFontFace(resources + "menu.ttf", true);
    Rml::LoadFontFace(resources + "menu-bold.ttf");
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
    if (!open_window(s_first_run ? Step::Welcome : Step::Disc)) {
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
        show_step(Step::Controls);
    }
}

void desktop_melee_welcome_how_to_play(void) {
    if (desktop_melee_welcome_busy()) {
        return;
    }
    s_loading_only = false;
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
