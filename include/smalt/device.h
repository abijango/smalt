// The supported Kobo models and their screens.
//
// Spec: Visual design ("Devices and scaling"), CLAUDE.md "Targets". Shared by
// the UI kit (palette and layout) and the Nickel side (which reads the model
// at startup).
#pragma once

#include <QString>

namespace smalt {

enum class DeviceModel {
    Unknown, // Falls back to the Clara Colour layout and colour palette.
    ClaraColour, // 6", 1448 x 1072, 300 ppi (colour layer 150 ppi).
    ClaraBW, // 6", 1448 x 1072, 300 ppi, monochrome palette.
    LibraColour, // 7", 1680 x 1264, 300 ppi (colour layer 150 ppi), page-turn buttons.
};

struct DeviceProfile
{
    DeviceModel model = DeviceModel::Unknown;
    QString name;
    int widthPx = 0; // Portrait width (short side).
    int heightPx = 0; // Portrait height (long side).
    int ppi = 300;
    bool colour = true;
    bool pageTurnButtons = false;
    int tabs = 4; // Home, Library, Series, More; Libra adds Shelves.
    int readingNowCovers = 3; // "3 covers on the Claras, 4 on the Libra"
    int upNextRows = 1; // "1 row on the Claras, 2 on the Libra"
};

inline DeviceProfile deviceProfile(DeviceModel model)
{
    DeviceProfile p;
    p.model = model;
    switch (model) {
    case DeviceModel::ClaraBW:
        p.name = QStringLiteral("Kobo Clara BW");
        p.widthPx = 1072;
        p.heightPx = 1448;
        p.colour = false;
        break;
    case DeviceModel::LibraColour:
        p.name = QStringLiteral("Kobo Libra Colour");
        p.widthPx = 1264;
        p.heightPx = 1680;
        p.pageTurnButtons = true;
        p.tabs = 5;
        p.readingNowCovers = 4;
        p.upNextRows = 2;
        break;
    case DeviceModel::ClaraColour:
    case DeviceModel::Unknown:
        p.name = model == DeviceModel::Unknown ? QStringLiteral("Unknown Kobo")
                                               : QStringLiteral("Kobo Clara Colour");
        p.widthPx = 1072;
        p.heightPx = 1448;
        break;
    }
    return p;
}

} // namespace smalt
