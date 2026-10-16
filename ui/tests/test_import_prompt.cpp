// The missing-assets prompt (import_prompt.hpp): fake services, no files.
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/import_prompt.hpp"

using namespace tt2ui;
using Stage = AssetImportJob::Stage;

namespace {
// Fake services that record every call.
struct Fake {
    PickResult pick{PickResult::Kind::Picked, "C:/Games/Text Twist 2"};
    std::vector<Stage> stages{Stage::VerifySource, Stage::Copy, Stage::ReadyToCommit};
    bool commit_ok = true;
    ImportError error;   // used when a stage is Failed
    std::size_t at = 0;
    int picks = 0, starts = 0, steps = 0, commits = 0, imported = 0, sounds = 0;
    std::string started_with;
    std::size_t last_budget = 0;

    ImportPromptServices services() {
        ImportPromptServices s;
        s.pick_folder = [this] {
            ++picks;
            return pick;
        };
        s.start_job = [this](const std::string& folder) {
            ++starts;
            started_with = folder;
        };
        s.step_job = [this](std::size_t budget) {
            ++steps;
            last_budget = budget;
            if (at + 1 < stages.size()) ++at;
        };
        s.commit_job = [this] {
            ++commits;
            stages.push_back(commit_ok ? Stage::Done : Stage::Failed);
            if (!commit_ok) error.kind = ImportError::Kind::CommitFailed;
            at = stages.size() - 1;
            return commit_ok;
        };
        s.job_view = [this] {
            ImportJobView v;
            v.stage = starts ? stages[at] : Stage::CheckSource;
            v.progress = static_cast<float>(at) / static_cast<float>(stages.size());
            v.error = error;
            return v;
        };
        s.on_imported = [this] { ++imported; };
        s.play_sound = [this](SoundId) { ++sounds; };
        return s;
    }
};

void run_until_result(ImportPromptFlow& f) {
    for (int i = 0; i < 100 && f.state() != ImportPromptFlow::State::Result; ++i) f.tick(16);
}
}  // namespace

TEST(import_prompt_shows_the_approved_wording) {
    Fake fake;
    const ImportPromptFlow f(fake.services());   // no line breaker: the text stays whole
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Prompt);
    // Literals, not the constants: an accidental edit of the wording fails here.
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Original game required"));
    ASSERT_EQ(f.dialog().label("MESSAGE")->text,
              std::string("This game needs the art, fonts and sounds from your own copy of Text Twist 2. The "
                          "replacement placeholder art isn't ready yet, so without them the game won't look or "
                          "sound right. Import them from your Text Twist 2 folder now?"));
    ASSERT_TRUE(f.dialog().button("BUTTON_YES") != nullptr);
    ASSERT_TRUE(f.dialog().button("BUTTON_NO") != nullptr);
}

TEST(import_prompt_yes_picks_runs_and_reports_success) {
    Fake fake;
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Picking);
    ASSERT_EQ(fake.picks, 0);   // the picker opens on the next tick, not inside the click
    f.tick(16);
    ASSERT_EQ(fake.picks, 1);
    ASSERT_EQ(fake.starts, 1);
    ASSERT_EQ(fake.started_with, std::string("C:/Games/Text Twist 2"));
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Running);
    run_until_result(f);
    ASSERT_EQ(fake.commits, 1);
    ASSERT_EQ(fake.imported, 1);
    ASSERT_TRUE(f.succeeded());
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Import complete"));
    ASSERT_TRUE(f.dialog().button("BUTTON_OK") != nullptr);
    f.press("BUTTON_OK");
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);
    ASSERT_EQ(fake.imported, 1);   // once
}

TEST(import_prompt_progress_label_follows_the_job) {
    Fake fake;
    fake.stages = {Stage::VerifySource, Stage::VerifySource, Stage::Copy, Stage::VerifyCopy, Stage::ReadyToCommit};
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    f.tick(16);   // pick + start: 0%
    ASSERT_EQ(f.dialog().label("PROGRESS")->text, std::string("Importing... 0%"));
    f.tick(16);   // one step: stage 1 of 5
    ASSERT_EQ(f.dialog().label("PROGRESS")->text, std::string("Importing... 20%"));
    ASSERT_EQ(fake.last_budget, IMPORT_STEP_BYTES);
    ASSERT_TRUE(f.dialog().buttons.empty());   // nothing to press while it runs
    ASSERT_EQ(import_progress_text(1.5f), std::string("Importing... 100%"));
    ASSERT_EQ(import_progress_text(-1.0f), std::string("Importing... 0%"));
}

TEST(import_prompt_keys_return_is_yes_escape_is_no) {
    Fake a;
    ImportPromptFlow yes(a.services());
    yes.key_return();
    ASSERT_TRUE(yes.state() == ImportPromptFlow::State::Picking);
    Fake b;
    ImportPromptFlow no(b.services());
    no.key_escape();
    ASSERT_TRUE(no.state() == ImportPromptFlow::State::Closed);
}

TEST(import_prompt_no_closes_without_picking) {
    Fake fake;
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_NO");
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);
    for (int i = 0; i < 5; ++i) f.tick(16);
    ASSERT_EQ(fake.picks, 0);
    ASSERT_EQ(fake.starts, 0);
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);   // never reopens itself
    f.press("BUTTON_YES");
    f.key_return();
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);
}

TEST(import_prompt_cancelled_pick_returns_to_the_prompt) {
    Fake fake;
    fake.pick = PickResult{PickResult::Kind::Cancelled, ""};
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    f.tick(16);
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Prompt);
    ASSERT_EQ(fake.starts, 0);
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Original game required"));
}

TEST(import_prompt_without_a_picker_shows_the_terminal_hint) {
    Fake fake;
    fake.pick = PickResult{};   // Unavailable
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    f.tick(16);
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Result);
    ASSERT_EQ(fake.starts, 0);
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("No folder picker"));
    ASSERT_TRUE(f.dialog().label("MESSAGE")->text.find("tt2 --import \"") != std::string::npos);
    ASSERT_TRUE(!f.succeeded());
    f.key_escape();
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);
}

TEST(import_prompt_failed_job_shows_the_error_and_never_imports) {
    Fake fake;
    fake.stages = {Stage::VerifySource, Stage::Failed};
    fake.error.kind = ImportError::Kind::Mismatch;
    fake.error.count = 3;
    fake.error.paths = {"images/gui/a.png", "sound/b.ogg", "fonts/c.xml"};
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    run_until_result(f);
    ASSERT_EQ(fake.commits, 0);
    ASSERT_EQ(fake.imported, 0);
    ASSERT_TRUE(!f.succeeded());
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Import failed"));
    ASSERT_EQ(f.dialog().label("MESSAGE")->text,
              std::string("3 files did not match.\nNothing was copied.\nimages/gui/a.png\nsound/b.ogg\nfonts/c.xml"));
}

TEST(import_prompt_failed_commit_keeps_the_old_assets) {
    Fake fake;
    fake.commit_ok = false;
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    run_until_result(f);
    ASSERT_EQ(fake.commits, 1);
    ASSERT_EQ(fake.imported, 0);
    ASSERT_EQ(f.dialog().label("MESSAGE")->text,
              std::string("The files could not be copied.\nYour previous assets are unchanged."));
}

TEST(import_failure_text_for_each_kind) {
    ImportError e;
    e.kind = ImportError::Kind::NotAnInstall;
    ASSERT_EQ(import_failure_text(e), std::string("That folder is not a Text Twist 2 install.\nNothing was copied."));
    e.kind = ImportError::Kind::Missing;
    e.count = 1;
    e.paths = {"sound/click.ogg"};
    ASSERT_EQ(import_failure_text(e), std::string("1 file missing from that folder.\nNothing was copied.\nsound/click.ogg"));
    e.kind = ImportError::Kind::Mismatch;
    e.count = 8;
    e.paths = {"a", "b", "c", "d", "e"};   // the job keeps 5; the dialog lists 3
    ASSERT_EQ(import_failure_text(e), std::string("8 files did not match.\nNothing was copied.\na\nb\nc"));
    e.kind = ImportError::Kind::BadArchive;   // the Android ZIP importer; never lists paths
    ASSERT_EQ(import_failure_text(e), std::string("That file is not a usable ZIP archive.\nNothing was copied."));
    ASSERT_EQ(std::string(to_string(e.kind)), std::string("not a usable ZIP archive"));
}

TEST(import_failure_text_never_shows_the_user_folder) {
    // A commit failure records the absolute per-user folder; the dialog
    // must not show it (it names the user's account).
    ImportError e;
    e.count = 1;
    e.paths = {"/home/someone/.local/share/re-twist-tt2/imported-assets"};
    for (auto kind : {ImportError::Kind::CommitFailed, ImportError::Kind::WriteFailed}) {
        e.kind = kind;
        const std::string text = import_failure_text(e);
        ASSERT_EQ(text, std::string("The files could not be copied.\nYour previous assets are unchanged."));
        ASSERT_TRUE(text.find("/home/") == std::string::npos);
    }
}

TEST(import_prompt_ignores_hidden_disabled_and_unknown_buttons) {
    Fake fake;
    ImportPromptFlow f(fake.services());
    f.dialog().button("BUTTON_YES")->enabled = false;
    f.press("BUTTON_YES");
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Prompt);
    f.dialog().button("BUTTON_YES")->enabled = true;
    f.dialog().button("BUTTON_YES")->sprite.visible = false;
    f.press("BUTTON_YES");
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Prompt);
    f.press("BUTTON_OK");   // not on this dialog
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Prompt);
    ASSERT_EQ(fake.sounds, 0);
}

TEST(import_prompt_wraps_the_message_with_the_breaker) {
    Fake fake;
    ImportPromptServices s = fake.services();
    s.wrap = [](const std::string& text, int) { return text.substr(0, 10) + "\n" + text.substr(10); };
    const ImportPromptFlow f(s);
    ASSERT_EQ(f.dialog().label("MESSAGE")->text.substr(0, 11), std::string("This game \n"));
    ASSERT_TRUE(f.dialog().h > YESNO_MIN_H - 1);
}

TEST(import_dialogs_are_wider_and_centred) {
    Fake fake;
    fake.pick = PickResult{};
    ImportPromptFlow f(fake.services());
    ASSERT_EQ(f.dialog().w, IMPORT_DIALOG_W);
    ASSERT_TRUE(f.dialog().label("HEADER")->x == IMPORT_DIALOG_W / 2.0f);
    ASSERT_TRUE(f.dialog().button("BUTTON_YES")->sprite.x == IMPORT_DIALOG_W / 2.0f - 10);
    f.press("BUTTON_YES");
    f.tick(16);   // no picker: the result dialog
    ASSERT_EQ(f.dialog().w, IMPORT_DIALOG_W);
    ASSERT_TRUE(f.dialog().button("BUTTON_OK")->sprite.x == IMPORT_DIALOG_W / 2.0f);
}

TEST(build_yes_no_default_width_is_the_original_layout) {
    // The original dialogs keep 400 px, centred on x = 200.
    const Dialog d = build_yes_no("PASS?", "Do you want to use a pass", false);
    ASSERT_EQ(d.w, YESNO_MIN_W);
    ASSERT_TRUE(d.label("HEADER")->x == 200.0f);
    ASSERT_TRUE(d.button("BUTTON_YES")->sprite.x == 190.0f);
    ASSERT_TRUE(d.button("BUTTON_NO")->sprite.x == 210.0f);
    ASSERT_EQ(build_yes_no("x", "y", true, {}, 300).w, YESNO_MIN_W);   // never narrower
}

TEST(import_prompt_shows_a_choose_folder_dialog_while_picking) {
    Fake fake;
    ImportPromptFlow f(fake.services());
    f.press("BUTTON_YES");
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Choose your folder"));
    ASSERT_TRUE(f.dialog().buttons.empty());   // the native dialog has the focus
    f.key_return();                            // ignored while picking
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Picking);
}

TEST(import_from_settings_starts_at_the_picker) {
    Fake fake;
    ImportPromptFlow f(fake.services(), false);
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Picking);
    ASSERT_EQ(fake.picks, 0);
    run_until_result(f);
    ASSERT_EQ(fake.picks, 1);
    ASSERT_EQ(fake.imported, 1);
    ASSERT_EQ(f.dialog().label("HEADER")->text, std::string("Import complete"));
}

TEST(import_from_settings_cancelled_pick_closes) {
    Fake fake;
    fake.pick = PickResult{PickResult::Kind::Cancelled, ""};
    ImportPromptFlow f(fake.services(), false);
    f.tick(16);
    ASSERT_TRUE(f.state() == ImportPromptFlow::State::Closed);   // no launch warning to go back to
    ASSERT_EQ(fake.starts, 0);
}
