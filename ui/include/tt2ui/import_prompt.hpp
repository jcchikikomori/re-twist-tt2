// The missing-assets prompt (docs/design/import-assets.md), headless.
//
// Not in the original game: the port needs the player's own Text Twist 2
// install for its art, fonts and sounds (OP-8, docs/plans/import-assets.md):
//   1. Prompt   the launch warning, "Original game required"   YES / NO
//   2. Picking  the native folder dialog (tinyfiledialogs)
//   3. Running  verify, then copy into the per-user asset folder
//   4. Result   done, failed, or "no folder picker: use tt2 --import"
//
//   Prompt --YES--> Picking --path--> Running --Done--> Result(ok) --OK--> Closed
//      |               | cancelled -> Prompt   |Failed--> Result(error) --OK--> Closed
//      |               | unavailable -> Result(hint) --OK--> Closed
//      +--NO----------------------------------------------------------> Closed
//
// `tt2 --import DIR` (main.cpp) is the headless equivalent of steps 2-3.
// main.cpp supplies the services, routes input and draws the dialog.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include "tt2ui/asset_import.hpp"
#include "tt2ui/folder_picker.hpp"
#include "tt2ui/game_dialogs.hpp"

namespace tt2ui {

namespace import_strings {
// The approved launch warning (OP-5): the README quotes it word for word.
inline constexpr const char* PROMPT_HEADER = "Original game required";
inline constexpr const char* PROMPT_BODY =
    "This game needs the art, fonts and sounds from your own copy of Text Twist 2. The replacement placeholder "
    "art isn't ready yet, so without them the game won't look or sound right. Import them from your Text Twist 2 "
    "folder now?";
inline constexpr const char* PROGRESS_HEADER = "Importing";
inline constexpr const char* DONE_HEADER = "Import complete";
inline constexpr const char* DONE_BODY = "The original art, fonts and sounds are ready.";
inline constexpr const char* FAILED_HEADER = "Import failed";
inline constexpr const char* NO_PICKER_HEADER = "No folder picker";
inline constexpr const char* NO_PICKER_BODY =
    "This system has no folder picker. Import from a terminal instead:\ntt2 --import \"<your Text Twist 2 folder>\"";
}  // namespace import_strings

inline constexpr std::size_t IMPORT_STEP_BYTES = 4u << 20;   // per frame, at most
// Wider than the original 400 px yes/no: the header and the paths are
// longer than any original dialog text.
inline constexpr int IMPORT_DIALOG_W = 600;
inline constexpr std::size_t IMPORT_LISTED_PATHS = 3;        // paths shown in the failure dialog

// "Importing... 42%"
inline std::string import_progress_text(float progress) {
    int pct = static_cast<int>(progress * 100.0f);
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    return "Importing... " + std::to_string(pct) + "%";
}

// The failure dialog's message; it always says whether anything changed.
inline std::string import_failure_text(const ImportError& e) {
    const std::string n = std::to_string(e.count);
    const char* files = e.count == 1 ? " file" : " files";
    std::string text;
    switch (e.kind) {
    case ImportError::Kind::NotAnInstall: return "That folder is not a Text Twist 2 install.\nNothing was copied.";
    case ImportError::Kind::EmptyManifest: return "The list of expected files is empty.\nNothing was copied.";
    case ImportError::Kind::Mismatch: text = n + files + " did not match.\nNothing was copied."; break;
    case ImportError::Kind::Missing: text = n + files + " missing from that folder.\nNothing was copied."; break;
    case ImportError::Kind::WriteFailed:
    case ImportError::Kind::CommitFailed:
        text = "The files could not be copied.\nYour previous assets are unchanged.";
        break;
    case ImportError::Kind::None: return "";
    }
    for (std::size_t i = 0; i < e.paths.size() && i < IMPORT_LISTED_PATHS; ++i) text += "\n" + e.paths[i];
    return text;
}

// What the flow needs to know about the running import.
struct ImportJobView {
    AssetImportJob::Stage stage = AssetImportJob::Stage::CheckSource;
    float progress = 0.0f;
    ImportError error;
};

struct ImportPromptServices {
    std::function<PickResult()> pick_folder;                // blocks while the native dialog is open
    std::function<void(const std::string& folder)> start_job;
    std::function<void(std::size_t byte_budget)> step_job;
    std::function<bool()> commit_job;                       // frees the loaded assets first, then commits
    std::function<ImportJobView()> job_view;
    std::function<void()> on_imported;                      // reload from the new folder
    std::function<void(SoundId)> play_sound;
    LineBreaker wrap;                                       // FONT_SETTINGSTEXT line breaks
};

class ImportPromptFlow {
public:
    enum class State { Prompt, Picking, Running, Result, Closed };

    explicit ImportPromptFlow(ImportPromptServices s) : s_(std::move(s)) { show_prompt(); }

    State state() const { return state_; }
    bool open() const { return state_ != State::Closed; }
    bool succeeded() const { return succeeded_; }
    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }

    void press(const std::string& name) {
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return;
        sound(SoundId::Button);
        if (state_ == State::Prompt) {
            if (name == "BUTTON_YES") state_ = State::Picking;   // the picker opens on the next tick
            else close();
        } else if (state_ == State::Result) {
            close();
        }
    }
    void key_return() {
        if (state_ == State::Prompt) press("BUTTON_YES");
        else if (state_ == State::Result) press("BUTTON_OK");
    }
    void key_escape() {
        if (state_ == State::Prompt) press("BUTTON_NO");
        else if (state_ == State::Result) press("BUTTON_OK");
    }

    void tick(int dt_ms) {
        if (state_ == State::Picking) pick();
        else if (state_ == State::Running) run();
        dialog_tick(dialog_, dt_ms);
    }

private:
    void sound(SoundId id) {
        if (s_.play_sound) s_.play_sound(id);
    }

    void show_prompt() {
        state_ = State::Prompt;
        dialog_ = build_yes_no(import_strings::PROMPT_HEADER, import_strings::PROMPT_BODY, false, s_.wrap,
                               IMPORT_DIALOG_W);
    }
    void show_result(const char* header, const std::string& text) {
        state_ = State::Result;
        dialog_ = build_yes_no(header, text, true, s_.wrap, IMPORT_DIALOG_W);
    }
    void show_progress(float progress) {
        if (state_ != State::Running || !dialog_.label("PROGRESS")) {
            Dialog d;
            d.w = IMPORT_DIALOG_W;
            d.h = YESNO_MIN_H;
            const float cx = IMPORT_DIALOG_W / 2.0f;
            d.labels.push_back(TextLabel{"HEADER", "FONT_DIALOG_HEADERS2", import_strings::PROGRESS_HEADER, cx, 13,
                                         TextAlign::Center, Anchor::Top, -1});
            d.labels.push_back(TextLabel{"PROGRESS", "FONT_SETTINGSTEXT", "", cx, 87, TextAlign::Center,
                                         Anchor::Top, -1, IMPORT_DIALOG_W - 10});
            dialog_ = std::move(d);
        }
        state_ = State::Running;
        dialog_.label("PROGRESS")->text = import_progress_text(progress);
    }
    void close() { state_ = State::Closed; }

    void pick() {
        const PickResult r = s_.pick_folder ? s_.pick_folder() : PickResult{};
        switch (r.kind) {
        case PickResult::Kind::Cancelled: show_prompt(); break;
        case PickResult::Kind::Unavailable:
            show_result(import_strings::NO_PICKER_HEADER, import_strings::NO_PICKER_BODY);
            break;
        case PickResult::Kind::Picked:
            if (s_.start_job) s_.start_job(r.path);
            show_progress(0.0f);
            break;
        }
    }

    void run() {
        ImportJobView v = view();
        using Stage = AssetImportJob::Stage;
        if (v.stage != Stage::ReadyToCommit && v.stage != Stage::Done && v.stage != Stage::Failed) {
            if (s_.step_job) s_.step_job(IMPORT_STEP_BYTES);
            v = view();
        }
        if (v.stage == Stage::ReadyToCommit) {
            if (s_.commit_job) s_.commit_job();
            v = view();
        }
        if (v.stage == Stage::Done) {
            succeeded_ = true;
            if (s_.on_imported) s_.on_imported();
            show_result(import_strings::DONE_HEADER, import_strings::DONE_BODY);
        } else if (v.stage == Stage::Failed) {
            show_result(import_strings::FAILED_HEADER, import_failure_text(v.error));
        } else {
            show_progress(v.progress);
        }
    }
    ImportJobView view() const { return s_.job_view ? s_.job_view() : ImportJobView{}; }

    ImportPromptServices s_;
    State state_ = State::Prompt;
    Dialog dialog_;
    bool succeeded_ = false;
};

}  // namespace tt2ui
