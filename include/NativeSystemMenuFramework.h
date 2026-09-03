// SPDX-License-Identifier: 0BSD
//
// Copyright (c) 2026 RoseEden
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// This header alone is 0BSD, so it can be copied into a mod under any license.
// The plugin itself is GPL-3.0.

// NativeSystemMenuFramework public API - drop this single header into your own
// SKSE plugin, no linking required (resolved via GetProcAddress at runtime).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace NativeSystemMenuFramework
{
    enum class SettingType
    {
        kSlider = 0,    // 0.0-1.0
        kDropdown = 1,  // index into options
        kCheckbox = 2,  // 0 or 1
        kLabel = 3,     // read-only text - see AddVanillaLabel
        kButton = 4,    // fire-and-forget action - see AddVanillaButton
    };

    namespace Internal
    {
        inline constexpr const wchar_t* kModuleName = L"NativeSystemMenuFramework";

        // SKSE loads a plugin and calls it before touching the next, so a mod
        // ahead of the framework in that order asks before it is there. Only
        // a success is kept; a miss is retried on the next call.
        inline HMODULE GetModule()
        {
            static HMODULE module = nullptr;
            if (!module)
                module = GetModuleHandleW(kModuleName);
            return module;
        }

        template <class T>
        T GetFunction(T& a_cache, LPCSTR a_name)
        {
            if (!a_cache) {
                if (auto* module = GetModule())
                    a_cache = reinterpret_cast<T>(GetProcAddress(module, a_name));
            }
            return a_cache;
        }

        // This header is compiled into your plugin, so this string lives in
        // your module and nowhere else. Two mods cannot overwrite each
        // other's name, whatever order they register in.
        inline std::string& Owner()
        {
            static std::string owner;
            return owner;
        }
    }

    // True when the framework is loaded and its API can be called. Every
    // function below is safe without it - they just return false - so this is
    // only worth asking to skip work you would otherwise do for nothing.
    inline bool IsInstalled() { return Internal::GetModule() != nullptr; }

    // Names the mod behind everything you register afterwards. Call it once,
    // before your first Add* call.
    //
    // The framework keeps one mod's tabs together and orders mods against
    // each other predictably. Without a name your tabs are still added, they
    // just aren't grouped with anything.
    //
    // This is an identifier, never shown to the player: use your DLL's name,
    // because it is also the name of your translation files
    // (Interface/Translations/<name>_<language>.txt).
    inline void SetModName(const char* a_name) { Internal::Owner() = a_name ? a_name : ""; }

    // Runs on the System menu's own update tick when the entry is selected -
    // not the render thread, not mid-frame. Keep it cheap.
    using EntryCallback = void(__stdcall*)();

    using AddSystemMenuEntryFunction = bool (*)(
        const char* text, EntryCallback callback, const char* jumpToTab, const char* owner);

    // Adds one entry to Skyrim's own System (Escape) menu, alongside Save/
    // Load/Settings/Controls/Help/Quit.
    //
    // a_jumpToTab opens that tab's settings directly, so the entry acts as its
    // own settings page. Takes a native tab ("Gameplay"/"Display"/"Audio") or
    // any custom tab name. a_callback still runs if given.
    //
    // With neither, the entry opens your own tabs as a list - the usual choice
    // for a mod with more than one tab. That needs SetModName.
    //
    // Returns false if the framework isn't installed, a_text is empty, or the
    // entry has nothing to do. Safe to call unconditionally: this is an
    // optional enhancement, not a hard dependency.
    inline bool AddSystemMenuEntry(
        const char* a_text, EntryCallback a_callback = nullptr, const char* a_jumpToTab = nullptr)
    {
        static AddSystemMenuEntryFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "AddSystemMenuEntry");
        return func ? func(a_text, a_callback, a_jumpToTab, Internal::Owner().c_str()) : false;
    }

    using TranslateStringFunction = void (*)(const char* key, char* buffer, int bufferSize);

    // The translation for a "$key", or the key itself when nothing knows it.
    // Truncated past 512 bytes.
    //
    // Only needed for text your own code assembles - a value glued to a label,
    // a name picked from a list. Anything handed to the framework whole is
    // resolved for you.
    inline std::string Translate(const char* a_key)
    {
        static TranslateStringFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "TranslateString");
        if (!func)
            return a_key ? a_key : "";

        char buffer[512] = {};
        func(a_key, buffer, static_cast<int>(std::size(buffer)));
        return buffer;
    }

    // Fills the text shown when the item is selected. Called on the menu's
    // own update tick, each time the item is opened - so it can report
    // something that changes, not only fixed prose. Up to 4096 bytes.
    using PageGetText = void(__stdcall*)(char* buffer, int bufferSize);

    using AddSystemMenuPageItemFunction = bool (*)(
        const char* page, const char* label, PageGetText getText, const char* owner);

    // Adds one item to a page of your own: a list of items, each opening a
    // panel of text. Built on the game's own Help screen, so the list and the
    // text both scroll. Good for a readme, a changelog or a stats screen -
    // anything a player reads rather than sets.
    //
    // The page is created the first time a name is used and gets its own entry
    // in the System menu. Later calls with the same name add to it, in call
    // order.
    //
    // Returns false if the framework isn't installed, or an argument is empty
    // or null.
    inline bool AddSystemMenuPageItem(const char* a_page, const char* a_label, PageGetText a_getText)
    {
        static AddSystemMenuPageItemFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "AddSystemMenuPageItem");
        return func ? func(a_page, a_label, a_getText, Internal::Owner().c_str()) : false;
    }

    // Label rows only: a setting row carries a widget on the right, so its
    // text has nowhere to move.
    enum class TextAlign
    {
        kLeft = 0,
        kCenter = 1,
        kRight = 2,
    };

    // Runs on the System menu's own update tick, same constraints as
    // EntryCallback. a_value is in the widget's own units: kSlider 0.0-1.0,
    // kDropdown = selected option's index, kCheckbox 0/1.
    //
    // The getter is polled every tick for as long as the row is on screen, so
    // it must be cheap and must not allocate its answer.
    //
    // A ScrollBar reports every step of a drag, so a setter can run dozens of
    // times a second. Keep it to applying the value; put anything expensive,
    // an ini write above all, in SettingCommit below.
    using SettingGetter = float(__stdcall*)();
    using SettingSetter = void(__stdcall*)(float a_value);

    // Runs once the value settles: the thumb let go, the value held still,
    // the selection moved away, or the menu closed. A checkbox or dropdown
    // has nothing to drag and commits as soon as it changes.
    //
    // Always preceded by the SettingSetter call carrying the same value.
    using SettingCommit = void(__stdcall*)(float a_value);

    // Returning false greys the row out and blocks its changes - onChange is
    // not called while disabled, so you needn't guard against it yourself.
    // Asked every tick while the row is on screen, so keep it to a comparison.
    using SettingIsEnabled = bool (__stdcall*)();

    // kSlider only - a ScrollBar has no readout of its own. Appends the result
    // to the row's label, live as it is dragged, e.g. "60 fps" rather than a
    // blind percentage.
    using SettingFormatValue = void (__stdcall*)(float value, char* buffer, int bufferSize);

    using AddVanillaSettingFunction = bool (*)(const char* tab, int type, const char* label, SettingGetter getValue,
        SettingSetter onChange, float defaultValue, const char* const* options, int optionCount,
        SettingIsEnabled isEnabled, SettingFormatValue formatValue, const char* description, const char* owner,
        SettingCommit onCommit);

    // Adds a real vanilla setting row, using the same ScrollBar/OptionStepper/
    // CheckBox widgets Bethesda does.
    //
    // a_tab is "Gameplay", "Display" or "Audio" for a native tab, or any other
    // name to create one the first time it is used. a_defaultValue (same units
    // as a_getValue/a_onChange) hooks the row into vanilla's own "reset
    // settings to default". a_options applies to kDropdown only.
    // a_description, if given, shows under the rows while the row is selected.
    // a_onCommit, if given, runs when the value settles - see SettingCommit.
    //
    // Returns false if the framework isn't installed, a_tab or a_label is
    // empty, or a_onChange is null.
    inline bool AddVanillaSetting(const char* a_tab, SettingType a_type, const char* a_label,
        SettingGetter a_getValue, SettingSetter a_onChange, float a_defaultValue,
        const std::vector<std::string>& a_options = {}, SettingIsEnabled a_isEnabled = nullptr,
        SettingFormatValue a_formatValue = nullptr, const char* a_description = nullptr,
        SettingCommit a_onCommit = nullptr)
    {
        static AddVanillaSettingFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "AddVanillaSetting");
        if (!func)
            return false;

        std::vector<const char*> options;
        options.reserve(a_options.size());
        for (const auto& option : a_options)
            options.push_back(option.c_str());

        return func(a_tab, static_cast<int>(a_type), a_label, a_getValue, a_onChange, a_defaultValue, options.data(),
            static_cast<int>(options.size()), a_isEnabled, a_formatValue, a_description, Internal::Owner().c_str(),
            a_onCommit);
    }

    // Text for a read-only row - no widget binds, so it is just a label.
    // Called every tick, so keep it cheap.
    using LabelGetText = void (__stdcall*)(char* buffer, int bufferSize);

    using AddVanillaLabelFunction = bool (*)(const char* tab, LabelGetText getText, int align, const char* owner);

    // Same tab rules as AddVanillaSetting. Returns false if the framework
    // isn't installed, a_tab is empty, or a_getText is null.
    inline bool AddVanillaLabel(const char* a_tab, LabelGetText a_getText, TextAlign a_align = TextAlign::kLeft)
    {
        static AddVanillaLabelFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "AddVanillaLabel");
        return func ? func(a_tab, a_getText, static_cast<int>(a_align), Internal::Owner().c_str()) : false;
    }

    // Runs on the System menu's own update tick, once per click.
    using ButtonPress = void (__stdcall*)();

    using AddVanillaButtonFunction = bool (*)(
        const char* tab, const char* label, ButtonPress onPress, const char* owner);

    // Vanilla has no button widget - this is its CheckBox, snapped back to
    // unchecked so it reads as a momentary press rather than a setting. Same
    // tab rules as AddVanillaSetting. Returns false if the framework isn't
    // installed, a_tab or a_label is empty, or a_onPress is null.
    inline bool AddVanillaButton(const char* a_tab, const char* a_label, ButtonPress a_onPress)
    {
        static AddVanillaButtonFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "AddVanillaButton");
        return func ? func(a_tab, a_label, a_onPress, Internal::Owner().c_str()) : false;
    }

    using SetVanillaTabDescriptionFunction = bool (*)(const char* tab, const char* description);

    // Shows text under the category list while a_tab is highlighted, the way a
    // setting row's description works. Empty clears it.
    inline bool SetVanillaTabDescription(const char* a_tab, const char* a_description)
    {
        static SetVanillaTabDescriptionFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "SetVanillaTabDescription");
        return func ? func(a_tab, a_description) : false;
    }

    using GetIniSettingFunction = float (*)(const char* name);

    // A real engine ini setting, e.g. "fDefaultWorldFOV:Display" - whichever
    // of Skyrim.ini/SkyrimPrefs.ini actually holds it. 0.0f if it doesn't exist.
    inline float GetIniSetting(const char* a_name)
    {
        static GetIniSettingFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "GetIniSetting");
        return func ? func(a_name) : 0.0f;
    }

    using SetIniSettingFunction = void (*)(const char* name, float value);

    // Live only - it doesn't persist to disk. Save the value in your own ini
    // and reapply it here on load.
    inline void SetIniSetting(const char* a_name, float a_value)
    {
        static SetIniSettingFunction cache = nullptr;
        const auto func = Internal::GetFunction(cache, "SetIniSetting");
        if (func)
            func(a_name, a_value);
    }
}
