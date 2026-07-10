#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// A named colour palette. The selected theme is a workstation preference:
// it persists in the shared SideDuck settings file, never in presets or
// project state.
struct Theme
{
    juce::String name;

    juce::Colour background;     // editor backdrop (gradient bottom)
    juce::Colour backgroundTop;  // editor backdrop (gradient top)
    juce::Colour panel;          // visualizer / popup panels
    juce::Colour panelBorder;
    juce::Colour control;        // buttons, combo boxes, knob tracks
    juce::Colour accent;
    juce::Colour accentSoft;     // dimmed accent for on-states / highlights
    juce::Colour text;
    juce::Colour dimText;
    juce::Colour knobBody;
    juce::Colour knobOutline;
    juce::Colour grid;           // visualizer grid lines (includes alpha)
    juce::Colour playhead;

    bool light = false;

    static const std::array<Theme, 7>& all()
    {
        static const std::array<Theme, 7> themes = { {
            { "Classic Dark",
              juce::Colour (0xff181b21), juce::Colour (0xff21252d),
              juce::Colour (0xff23272f), juce::Colour (0xff343a45),
              juce::Colour (0xff2c313b),
              juce::Colour (0xff4fc3f7), juce::Colour (0xff2a6f8f),
              juce::Colour (0xffd8dee9), juce::Colour (0xff7a8290),
              juce::Colour (0xff2b303a), juce::Colour (0xff3a4150),
              juce::Colour (0x12ffffff), juce::Colour (0xffffffff),
              false },

            { "Midnight Blue",
              juce::Colour (0xff0c1220), juce::Colour (0xff131c30),
              juce::Colour (0xff141e33), juce::Colour (0xff26324d),
              juce::Colour (0xff1c2940),
              juce::Colour (0xff5b9dff), juce::Colour (0xff2b4a7a),
              juce::Colour (0xffdce6f5), juce::Colour (0xff77839c),
              juce::Colour (0xff1a2438), juce::Colour (0xff2c3a58),
              juce::Colour (0x14ffffff), juce::Colour (0xffffffff),
              false },

            { "Light",
              juce::Colour (0xffe7eaf0), juce::Colour (0xfff4f6f9),
              juce::Colour (0xfffcfdfe), juce::Colour (0xffd0d6e0),
              juce::Colour (0xffe9edf2),
              juce::Colour (0xff2f7cf6), juce::Colour (0xffb9d4fb),
              juce::Colour (0xff21252c), juce::Colour (0xff69707e),
              juce::Colour (0xfff5f6f8), juce::Colour (0xffc4cbd6),
              juce::Colour (0x17000000), juce::Colour (0xdd333a45),
              true },

            { "Neon",
              juce::Colour (0xff0b0913), juce::Colour (0xff141020),
              juce::Colour (0xff161226), juce::Colour (0xff2e2450),
              juce::Colour (0xff1e1833),
              juce::Colour (0xffff3fd8), juce::Colour (0xff7a2fa8),
              juce::Colour (0xffeae6ff), juce::Colour (0xff8d85ad),
              juce::Colour (0xff201a38), juce::Colour (0xff3a2f5e),
              juce::Colour (0x16ffffff), juce::Colour (0xffffffff),
              false },

            { "Graphite",
              juce::Colour (0xff17181b), juce::Colour (0xff1f2125),
              juce::Colour (0xff202227), juce::Colour (0xff34373d),
              juce::Colour (0xff2a2d33),
              juce::Colour (0xffff9f43), juce::Colour (0xff8a5a26),
              juce::Colour (0xffe2e4e8), juce::Colour (0xff868b93),
              juce::Colour (0xff26282e), juce::Colour (0xff3c4047),
              juce::Colour (0x10ffffff), juce::Colour (0xffffffff),
              false },

            { "Emerald",
              juce::Colour (0xff0c1512), juce::Colour (0xff12201b),
              juce::Colour (0xff142420), juce::Colour (0xff26443a),
              juce::Colour (0xff1b332c),
              juce::Colour (0xff2fe6a8), juce::Colour (0xff1a7a5c),
              juce::Colour (0xffdcf2ea), juce::Colour (0xff74948a),
              juce::Colour (0xff183029), juce::Colour (0xff2b4c41),
              juce::Colour (0x12ffffff), juce::Colour (0xffffffff),
              false },

            { "Sunset",
              juce::Colour (0xff190f14), juce::Colour (0xff23151c),
              juce::Colour (0xff261720), juce::Colour (0xff452b3a),
              juce::Colour (0xff33202b),
              juce::Colour (0xffff6b5e), juce::Colour (0xff8f3a34),
              juce::Colour (0xfff5e6e8), juce::Colour (0xff9d8089),
              juce::Colour (0xff2b1b24), juce::Colour (0xff4a303e),
              juce::Colour (0x14ffffff), juce::Colour (0xffffffff),
              false },
        } };

        return themes;
    }

    static Theme byIndex (int index)
    {
        return all()[(size_t) juce::jlimit (0, (int) all().size() - 1, index)];
    }

    static juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "SideDuck";
        options.filenameSuffix      = ".settings";
        options.folderName          = "SideDuck";
        options.osxLibrarySubFolder = "Application Support";
        return options;
    }

    static int loadSavedIndex()
    {
        juce::PropertiesFile props (settingsOptions());
        return juce::jlimit (0, (int) all().size() - 1, props.getIntValue ("theme", 0));
    }

    static void saveIndex (int index)
    {
        juce::PropertiesFile props (settingsOptions());
        props.setValue ("theme", index);
        props.save();
    }
};
