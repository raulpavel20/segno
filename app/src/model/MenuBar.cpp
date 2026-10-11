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
               action("file.save"), action("file.saveAs"), separator(), action("project.export")}};
    if (!mac) // the application menu's on macOS
    {
        file.entries.push_back(separator());
        file.entries.push_back(action("app.quit"));
    }
    menus.push_back(std::move(file));

    menus.push_back(
        {"Edit",
         'e',
         {action("edit.undo"), action("edit.redo"), separator(), action("edit.delete"),
          action("edit.duplicate"), action("edit.split"), separator(), action("edit.selectAll")}});

    menus.push_back(
        {"Track",
         't',
         {action("track.add"), action("track.addReturn"), separator(), action("track.arm"),
          action("track.mute"), action("track.solo"), separator(), action("automation.addLane"),
          action("automation.assign"), action("automation.showLastTouched")}});

    menus.push_back(
        {"Clip",
         'c',
         {action("clip.newMidi"), separator(), action("clip.bounce"), action("clip.bounceToTrack"),
          action("clip.cancelBounces"), separator(), action("clip.mute"), action("clip.crossfade"),
          separator(),
          submenu("Takes", {action("view.takes"), action("take.previous"), action("take.next"), separator(),
                            action("take.useWhole"), action("take.rename"), action("take.copyToTrack"),
                            action("take.moveToTrack"), action("take.spread"), separator(),
                            action("take.keepOnly")}),
          submenu("Notes", {action("notes.quantise"), separator(), action("notes.up"), action("notes.down"),
                            action("notes.octaveUp"), action("notes.octaveDown"), separator(),
                            action("notes.later"), action("notes.earlier")})}});

    menus.push_back(
        {"Transport",
         'p',
         {action("transport.playStop"), action("transport.toStart"), separator(), action("transport.record"),
          action("transport.capture"), action("automation.arm"), action("automation.reEnable"), separator(), action("transport.countIn"),
          submenu("Count-in length", {list("countIn")}), submenu("MIDI loop recording", {list("midiLoop")}),
          separator(), action("transport.loop"), action("transport.metronome")}});

    menus.push_back({"View",
                     'v',
                     {action("view.mixer"), separator(), action("view.browser"), action("view.inspector"),
                      action("view.detail"), action("view.detailMode"), separator(), action("view.lanes"),
                      action("view.returns"), action("module.fold"), action("module.expand"), separator(),
                      action("view.zoomIn"), action("view.zoomOut"), action("view.zoomFit"),
                      action("view.follow"), separator(), action("view.focus"), action("view.fullScreen")}});

    Menu options{"Options",
                 'o',
                 {action("view.snap"), action("project.edgeFades"), separator(),
                  submenu("Computer keyboard",
                          {action("midi.computerKeyboard"), separator(), action("midi.octaveDown"),
                           action("midi.octaveUp"), action("midi.velocityDown"), action("midi.velocityUp")}),
                  separator()}};
    if (!mac)
        options.entries.push_back(action("app.audioSettings"));
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
    return {action("app.about"), separator(), action("app.audioSettings")};
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
