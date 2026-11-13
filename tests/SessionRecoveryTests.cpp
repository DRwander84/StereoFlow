#include "TestUtil.h"
#include "system/RecoveryJournal.h"
#include "system/RestorePolicy.h"
using namespace sf;
int main() {
    // decision table: (original, applied, current)
    CHECK(shouldUnmuteOnRestore(false, true, true));    // we muted it, still muted -> restore
    CHECK(!shouldUnmuteOnRestore(false, true, false));  // user un-muted meanwhile -> leave alone
    CHECK(!shouldUnmuteOnRestore(true, false, true));   // was muted by user before us -> stay muted
    CHECK(!shouldUnmuteOnRestore(true, true, true));    // never override original mute
    CHECK(!shouldUnmuteOnRestore(false, false, true));  // we did not apply it -> not ours

    std::vector<JournalEntry> v;
    v.push_back({1234, 132000000000ull, false, "{0.0.0.00000000}.{abc}", "{dev}|%b{guid}|1234"});
    v.push_back({99, 5, true, "dev2", "inst2"});
    auto text = serializeJournal(v);
    auto back = parseJournal(text);
    CHECK(back.size() == 2);
    CHECK(back[0].pid == 1234 && back[0].createTime == 132000000000ull && !back[0].originalMuted);
    CHECK(back[0].instanceId == "{dev}|%b{guid}|1234" && back[1].originalMuted);
    // volume column round-trips; old journals without it default to 1.0
    std::vector<JournalEntry> w(1); w[0].pid = 7; w[0].instanceId = "i"; w[0].originalVolume = 0.35f;
    auto wb = parseJournal(serializeJournal(w));
    CHECK(wb.size() == 1 && wb[0].originalVolume > 0.349f && wb[0].originalVolume < 0.351f && wb[0].instanceId == "i");
    CHECK(back[0].originalVolume == 1.0f);
    auto old = parseJournal("STEREOFLOW-JOURNAL 1\n5\t6\t0\tdev\tinst\n");
    CHECK(old.size() == 1 && old[0].instanceId == "inst" && old[0].originalVolume == 1.0f);
    CHECK(shouldRestoreVolume(true, kHushVolume));       // still hushed by us -> restore
    CHECK(!shouldRestoreVolume(true, 0.6f));             // user changed it -> leave alone
    CHECK(!shouldRestoreVolume(false, kHushVolume));     // not applied by us
    // leftovers: an unowned session at one of our hush depths is released, anything else is never touched
    CHECK(shouldReleaseOrphan(false, false, kHushVolume));
    CHECK(shouldReleaseOrphan(false, false, 0.00025f) && shouldReleaseOrphan(false, false, 0.004f));
    CHECK(!shouldReleaseOrphan(false, true, kHushVolume));     // inside a routed process tree: the engine owns it
    CHECK(!shouldReleaseOrphan(true, false, kHushVolume));     // system sounds
    CHECK(!shouldReleaseOrphan(false, false, 0.5f));           // a volume the user chose
    CHECK(!shouldReleaseOrphan(false, false, 0.0f));           // a real mute-by-volume is not ours
    CHECK(!shouldReleaseOrphan(false, false, 1.0f));
    CHECK(parseJournal("garbage").empty());
    CHECK(parseJournal("STEREOFLOW-JOURNAL 1\nnot\ta\tvalid\n").empty());
    CHECK(parseJournal(serializeJournal({})).empty());
    TEST_MAIN_END();
}
