// SPDX-License-Identifier: GPL-3.0-or-later
#include "disc_check.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../../src/pc/launcher_data.hpp"

namespace {
constexpr const char* k_pal =
    "This is the European (PAL) disc. Desktop Melee needs the USA version, 1.02.";
constexpr const char* k_modified =
    "This disc image has been modified or is a bad copy. Desktop Melee needs a "
    "clean, unmodified USA 1.02 image.";
constexpr const char* k_not_disc =
    "This file isn't a GameCube disc image. Choose your Melee .iso, .gcm, .ciso or .rvz file.";
}  // namespace

struct DesktopMeleeDiscCheck {
    std::string path;
    std::atomic_bool cancel{false};
    std::atomic_uint progress{0};
    std::mutex lock;
    DesktopMeleeDiscState state = DESKTOP_MELEE_DISC_CHECKING;
    std::string message;
    std::thread worker;

    void finish(DesktopMeleeDiscState s, std::string m) {
        std::lock_guard<std::mutex> guard(lock);
        state = s;
        message = std::move(m);
    }

    void run() {
        try {
            const auto info = launcher::inspect_disc(path);
            if (!info.supported) {
                // nod's own error ("disc format error: ...") means nothing to
                // a player.
                const bool unreadable = info.message.rfind("Cannot open disc image", 0) == 0;
                finish(DESKTOP_MELEE_DISC_REJECTED, unreadable ? k_not_disc : info.message);
                return;
            }
            // melee-pc runs the PAL disc experimentally; Desktop Melee does not.
            if (info.message.find("PAL") != std::string::npos) {
                finish(DESKTOP_MELEE_DISC_REJECTED, k_pal);
                return;
            }
            const auto verified = launcher::verify_disc(path, cancel, progress);
            switch (verified.state) {
            case launcher::VerifyState::Verified:
                finish(DESKTOP_MELEE_DISC_VERIFIED,
                       "Super Smash Bros. Melee · USA · 1.02 · verified");
                break;
            case launcher::VerifyState::Mismatch:
                finish(DESKTOP_MELEE_DISC_MODIFIED, k_modified);
                break;
            case launcher::VerifyState::Canceled:
            case launcher::VerifyState::Error:
                finish(DESKTOP_MELEE_DISC_ERROR, verified.message);
                break;
            }
        } catch (const std::exception& e) {
            finish(DESKTOP_MELEE_DISC_ERROR, std::string("Could not read this file: ") + e.what());
        }
    }
};

extern "C" DesktopMeleeDiscCheck* desktop_melee_disc_check_start(const char* path) {
    auto* check = new DesktopMeleeDiscCheck();
    check->path = path != nullptr ? path : "";
    check->worker = std::thread([check] { check->run(); });
    return check;
}

extern "C" DesktopMeleeDiscState desktop_melee_disc_check_poll(DesktopMeleeDiscCheck* check,
                                                               int* percent, char* message,
                                                               int message_size) {
    if (check == nullptr) {
        return DESKTOP_MELEE_DISC_ERROR;
    }
    if (percent != nullptr) {
        *percent = static_cast<int>(std::min(check->progress.load(), 100u));
    }
    std::lock_guard<std::mutex> guard(check->lock);
    if (message != nullptr && message_size > 0) {
        snprintf(message, static_cast<size_t>(message_size), "%s", check->message.c_str());
    }
    return check->state;
}

extern "C" void desktop_melee_disc_check_free(DesktopMeleeDiscCheck* check) {
    if (check == nullptr) {
        return;
    }
    check->cancel = true;
    if (check->worker.joinable()) {
        check->worker.join();
    }
    delete check;
}
