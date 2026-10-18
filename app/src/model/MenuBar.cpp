#include "model/MenuBar.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fd::model
{
namespace
{
MenuEntry action(std::string id)
{
    return {.kind = MenuEntry::Kind::Action, .id = std::move(id)};
}
MenuEntry separator()
{
    return {.kind = MenuEntry::Kind::Separator};
}
MenuEntry submenu(std::string title, std::vector<MenuEntry> entries)
{
    return {.kind = MenuEntry::Kind::Submenu, .title = std::move(title), .entries = std::move(entries)};
}
MenuEntry list(std::string id)
{
    return {.kind = MenuEntry::Kind::List, .id = std::move(id)};
}

void collect(const std::vector<MenuEntry>& entries, std::vector<std::string>& out)
{
    for (const auto& e : entries)
        if (e.kind == MenuEntry::Kind::Action)
            out.push_back(e.id);
        else if (e.kind == MenuEntry::Kind::Submenu)
            collect(e.entries, out);
}

void check(const std::vector<MenuEntry>& entries, const std::string& where, int depth,
           std::vector<std::string>& problems)
{
    if (entries.empty())
        problems.push_back(where + " is empty");
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (e.kind == MenuEntry::Kind::Separator)
        {
            if (i == 0)
                problems.push_back(where + " starts with a separator");
            else if (i + 1 == entries.size())
                problems.push_back(where + " ends with a separator");
            else if (entries[i - 1].kind == MenuEntry::Kind::Separator)
                problems.push_back(where + " has two separators in a row");
        }
        if (e.kind == MenuEntry::Kind::Submenu)
        {
            if (depth > 0)
                problems.push_back(where + " > " + e.title + " is a submenu inside a submenu");
            check(e.entries, where + " > " + e.title, depth + 1, problems);
        }
    }
}
} // namespace

std::vector<Menu> menuBar(MenuPlatform platform)
{
    const bool mac = platform == MenuPlatform::Mac;
    std::vector<Menu> menus;

    Menu file{"File",
              'f',
              {action("file.new"), action("file.open"), submenu("Open recent", {list("recent")}), separator(),
               action("file.save"), action("file.saveAs"), separator(), action("file.collect"),
               action("file.removeUnused"), separator(), action("project.export")}};
    if (!mac) // the application menu's on macOS
    {
        file.entries.push_back(separator());
        file.entries.push_back(action("app.quit"));
    }
    menus.push_back(std::move(file));

    menus.push_back(
        {"Edit",
         'e',
         {action("edit.undo"), action("edit.redo"), separator(), action("edit.cut"), action("edit.copy"),
          action("edit.paste"), separator(), action("edit.delete"), action("edit.duplicate"),
          action("edit.split"), separator(), action("edit.rename"), action("edit.selectAll"),
          action("edit.selectNone"),
          // A time selection (time-selection.md §6).
          action("edit.selectLoop"),
          submenu("Time selection", {action("edit.timeEndLater"), action("edit.timeEndEarlier")})}});

    menus.push_back(
        {"Track",
         't',
         {action("track.add"), action("track.addReturn"), separator(), action("track.duplicate"),
          action("track.delete"), submenu("Colour", {list("trackColour")}), separator(), action("track.arm"),
          action("track.mute"), action("track.solo"), separator(), action("automation.addLane"),
          action("automation.assign"), action("automation.showLastTouched")}});

    menus.push_back(
        {"Clip",
         'c',
         {action("clip.newMidi"), separator(), action("clip.bounce"), action("clip.bounceToTrack"),
          action("clip.cancelBounces"), action("clip.makeWavetable"), separator(), action("clip.mute"),
          action("clip.loop"), action("clip.crossfade"), action("clip.crop"),
          submenu("Colour", {list("clipColour")}), separator(),
          submenu("Takes", {action("view.takes"), action("take.previous"), action("take.next"), separator(),
                            action("take.useWhole"), action("take.rename"), action("take.copyToTrack"),
                            action("take.moveToTrack"), action("take.spread"), separator(),
                            action("take.keepOnly")})}});

    // The piano roll's (ADR 0058, piano-roll.md §13): what acts on notes, related commands one level
    // down so the menu stays short enough to open over the piano roll. The clip view's Notes button
    // opens this same menu.
    menus.push_back(
        {"Notes",
         'n',
         {action("notes.quantise"), action("notes.quantiseSettings"), separator(),
          submenu("Transpose", {action("notes.up"), action("notes.down"), separator(),
                                action("notes.semitoneUp"), action("notes.semitoneDown"), separator(),
                                action("notes.octaveUp"), action("notes.octaveDown")}),
          action("notes.fitToKey"), separator(), action("notes.makeChords"),
          submenu("Chords", {action("notes.changeChord"), separator(), action("notes.invertUp"),
                             action("notes.invertDown"), separator(), action("notes.voiceSmooth"),
                             action("notes.voiceClose"), action("notes.voiceOpen"),
                             action("notes.voiceDrop2"), action("notes.voiceDrop3"),
                             action("notes.voiceSpread"), separator(), action("notes.addBass")}),
          separator(), action("notes.join"),
          submenu("Length", {action("notes.legato"), action("notes.fixedLength"), separator(),
                             action("notes.doubleSpeed"), action("notes.halfSpeed")}),
          action("notes.reverse"), action("notes.mirror"), separator(),
          submenu("Transform",
                  {action("notes.tool.humanize"), action("notes.tool.strum"), action("notes.tool.arpeggiate"),
                   action("notes.tool.chop"), action("notes.tool.ornament"), action("notes.tool.velocity"),
                   action("notes.tool.span"), action("notes.tool.randomise")}),
          submenu("Generate", {action("notes.tool.rhythm"), action("notes.tool.melody"),
                               action("notes.tool.chords"), action("notes.tool.bassline")}),
          action("notes.reroll"), action("notes.keepChance"), separator(),
          submenu("Select", {action("notes.selectSamePitch"), action("notes.selectOutsideKey"),
                             action("notes.selectInLoop"), action("notes.invertSelection")}),
          action("notes.later"), action("notes.earlier"),
          // The clip view's time selection (time-selection.md §14.1): the Song menu's keys.
          submenu("Time",
                  {action("notes.insertTime"), action("notes.deleteTime"), action("notes.duplicateTime"),
                   separator(), action("notes.cropClip"), action("notes.loopClip")})}});

    // The audio editor's (ADR 0063, audio-editor.md §13): what acts on audio clips and the editor's
    // range. The bar's Audio button opens this same menu.
    menus.push_back(
        {"Audio",
         'a',
         {action("audio.warp"),
          submenu("Warp mode", {action("audio.warpMode.beats"), action("audio.warpMode.tones"),
                                action("audio.warpMode.repitch")}),
          submenu("Tempo", {action("audio.tempoDouble"), action("audio.tempoHalf"), separator(),
                            action("audio.detectTempo"), action("audio.songTempo")}),
          separator(),
          action("audio.quantise"),
          action("audio.quantiseSettings"),
          submenu("Warp markers", {action("audio.markersAtTransients"), action("audio.warpFromHere"),
                                   action("audio.setDownbeat"), action("audio.removeMarkers"), separator(),
                                   action("audio.addTransient"), action("audio.removeTransients")}),
          separator(),
          submenu("Transpose", {action("audio.semitoneUp"), action("audio.semitoneDown"), separator(),
                                action("audio.octaveUp"), action("audio.octaveDown")}),
          action("audio.followKey"),
          separator(),
          submenu("Gain",
                  {action("audio.gainUp"), action("audio.gainDown"), separator(), action("audio.silence")}),
          action("audio.normalise"),
          separator(),
          action("audio.reverse"),
          action("audio.polarity"),
          submenu("Channels", {action("audio.channels.stereo"), action("audio.channels.left"),
                               action("audio.channels.right"), action("audio.channels.mono"),
                               action("audio.channels.swapped")}),
          separator(),
          action("audio.crop"),
          action("audio.fadeRange"),
          action("audio.loopRange"),
          action("audio.slice")}});

    // Song-wide things: the song's key (ADR 0057, key-and-scale.md §13), then its sections (ADR 0049,
    // arranger.md §12).
    menus.push_back(
        {"Song",
         'g', // Alt+S solos; Notes has the n
         {action("song.key"), action("song.noKey"), separator(), action("song.addSection"),
          action("song.insertSection"), separator(), action("song.loopSection"),
          action("song.previousSection"), action("song.nextSection"), separator(),
          action("song.selectSectionTime"), action("song.duplicateSection"), action("song.deleteSection"),
          action("song.removeSectionMark"), separator(), submenu("Section colour", {list("sectionColour")}),
          // The whole song's time, sections and all (time-selection.md §9).
          separator(), action("song.insertTime"), action("song.deleteTime"), action("song.cutTime"),
          action("song.copyTime"), action("song.pasteTime"), action("song.duplicateTime"),
          // What's left to chance, as it played or rolling again (ADR 0062).
          separator(), action("song.keepAllRolls")}});

    // Presets act on the selected module (ADR 0044, presets.md §10).
    menus.push_back(
        {"Module",
         'd',
         {action("module.previousPreset"), action("module.nextPreset"), separator(),
          action("module.savePreset"), action("module.savePresetAs"), action("module.revertPreset"),
          separator(), action("module.compare"), action("module.copyCompare"), separator(),
          action("module.useAsDefault"), action("module.clearDefault"), separator(),
          action("module.presetsFolder"), separator(),
          // A module's own (docs/design/table-editor.md §8): dimmed for the others.
          action("module.editWavetable"), action("module.importWavetable"), action("module.exportWavetable"),
          separator(), action("module.loadSample"), action("module.loadSource1"),
          action("module.loadSource2"), action("module.swapSources"),
          // A Drum Kit's pads (drum-kit.md §12).
          action("module.renamePad"), action("module.copyPad"), action("module.pastePad"),
          action("module.clearPad"), separator(),
          // Racks (racks.md §9): a module of modules, its chains and macros.
          action("module.groupIntoRack"), action("module.ungroupRack"), action("module.editMacros"),
          action("module.addChain"), action("module.renameRack"), separator(),
          // Its snapshots and Morph (racks.md §18.9).
          action("module.storeSnapshot"), action("module.updateSnapshot"), action("module.renameSnapshot"),
          action("module.removeSnapshot"), action("module.morph"), action("module.vary"),
          action("module.varyMore"), separator(), action("module.saveRack"), action("module.saveRackAs"),
          action("module.racksFolder"), separator(),
          // MIDI effects (midi-effects.md §13): before the instrument, wherever the selection is.
          submenu("Add MIDI effect",
                  {action("module.addMidiEffect.arpeggiator"), action("module.addMidiEffect.chord"),
                   action("module.addMidiEffect.pitch")}),
          // Modulators (modulators.md §15): one added, the selected one's roll and routes.
          submenu("Add modulator",
                  {action("module.addModulator.lfo"), action("module.addModulator.random"),
                   action("module.addModulator.envelope"), action("module.addModulator.follower"),
                   action("module.addModulator.steps")}),
          action("module.keepRoll"), action("module.removeRoutes")}});

    menus.push_back(
        {"Transport",
         'p',
         {action("transport.playStop"), action("transport.toStart"), separator(), action("transport.record"),
          action("transport.capture"), action("automation.arm"), action("automation.reEnable"), separator(), action("transport.countIn"),
          submenu("Count-in length", {list("countIn")}), submenu("MIDI loop recording", {list("midiLoop")}),
          separator(), action("transport.loop"), action("transport.metronome")}});

    menus.push_back(
        {"View",
         'v',
         {action("view.mixer"), separator(), action("view.browser"), action("browser.search"),
          action("browser.preview"), action("view.inspector"), action("view.detail"),
          action("view.detailMode"), separator(), action("view.lanes"), action("view.returns"),
          action("view.songChords"), action("module.fold"), action("module.expand"),
          action("module.nextPage"), action("module.previousPage"), separator(),
          // The clip view's options (piano-roll.md §13).
          action("view.clipInKey"),
          submenu("Other tracks' notes", {action("view.clipOthers"), separator(), list("otherTracks")}),
          action("view.clipPlayed"), submenu("Clip view lane", {list("clipLane")}),
          // The audio editor's rows (audio-editor.md §4): its level column, by menu.
          submenu("Waveform height", {action("view.waveformTaller"), action("view.waveformShorter"),
                                      action("view.waveformReset")}),
          submenu("Tools pane", {action("view.toolsTransform"), action("view.toolsGenerate")}),
          submenu("Draw",
                  {action("view.drawNote"), action("view.drawChord"), action("view.drawChordUnder"),
                   separator(), action("view.drawTriads"), action("view.drawSevenths"),
                   action("view.drawNinths"), action("view.drawSus2"), action("view.drawSus4"),
                   action("view.drawAdd9"), separator(), action("view.voicingSmooth"),
                   action("view.voicingClose"), action("view.voicingOpen"), action("view.voicingDrop2"),
                   action("view.voicingDrop3"), action("view.voicingSpread")}),
          separator(), action("view.zoomIn"), action("view.zoomOut"), action("view.zoomFit"),
          action("view.follow"), separator(), action("view.focus"), action("view.fullScreen")}});

    Menu options{"Options",
                 'o',
                 {action("grid.narrower"), action("grid.wider"), action("grid.triplets"), action("view.snap"),
                  action("grid.fixed"), separator(), action("project.edgeFades"),
                  action("project.reducedLatency"), separator(),
                  submenu("Computer keyboard",
                          {action("midi.computerKeyboard"), separator(), action("midi.octaveDown"),
                           action("midi.octaveUp"), action("midi.velocityDown"), action("midi.velocityUp")}),
                  action("midi.mapMode"), separator()}};
    if (!mac)
        options.entries.push_back(action("app.settings"));
    options.entries.push_back(action("app.scanPlugins"));
    options.entries.push_back(action("app.assistants"));
    if (!mac)
    {
        options.entries.push_back(separator());
        options.entries.push_back(action("app.systemTitleBar"));
    }
    menus.push_back(std::move(options));

    Menu help{"Help", 'h', {action("app.commandPalette")}};
    if (!mac)
    {
        help.entries.push_back(separator());
        help.entries.push_back(action("app.about"));
    }
    menus.push_back(std::move(help));
    return menus;
}

std::vector<MenuEntry> macAppMenu()
{
    return {action("app.about"), separator(), action("app.settings")};
}

std::vector<std::string> menuActions(const std::vector<Menu>& menus)
{
    std::vector<std::string> out;
    for (const auto& m : menus)
        collect(m.entries, out);
    return out;
}

std::vector<std::string> menuProblems(const std::vector<Menu>& menus)
{
    std::vector<std::string> problems;
    std::set<char> mnemonics;
    for (const auto& m : menus)
    {
        check(m.entries, m.title, 0, problems);
        std::string lower = m.title;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (m.mnemonic == 0 || lower.find(m.mnemonic) == std::string::npos)
            problems.push_back(m.title + "'s mnemonic isn't in its title");
        else if (!mnemonics.insert(m.mnemonic).second)
            problems.push_back(m.title + "'s mnemonic is another menu's");
    }
    std::set<std::string> seen;
    for (const auto& id : menuActions(menus))
        if (!seen.insert(id).second)
            problems.push_back(id + " is in the menus twice");
    return problems;
}

int menuForMnemonic(const std::vector<Menu>& menus, char letter)
{
    const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    for (size_t i = 0; i < menus.size(); ++i)
        if (menus[i].mnemonic == lower)
            return static_cast<int>(i);
    return -1;
}
} // namespace fd::model
