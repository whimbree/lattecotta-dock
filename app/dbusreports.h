/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef DBUSREPORTS_H
#define DBUSREPORTS_H

// local
#include <coretypes.h>

// Qt
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>

// Plasma
#include <Plasma/Plasma>

namespace Latte {
class View;
}

namespace Latte {
namespace DbusReports {

//! The org.kde.LatteDock JSON read surface
//! (docs/dbus-observability-interface.md). Two layers, so serialization
//! stays a pure core: ViewRecord is a value snapshot of every field
//! viewsData() reports and the inline name/serialize functions below are
//! pure (unit-tested without a corona in tests/units/dbusreportstest.cpp);
//! only the collect* functions in dbusreports.cpp read live View objects.

struct ViewRecord {
    uint containmentId{0};
    QString layout;
    bool isCloned{false};
    int isClonedFrom{-1};
    Types::ViewType type{Types::DockView};
    QString screen;
    bool onPrimary{false};
    Plasma::Types::Location edge{Plasma::Types::Floating};
    Types::Alignment alignment{Types::NoneAlignment};
    Types::Visibility visibilityMode{Types::None};
    bool isHidden{false};
    bool inStartup{false};
    bool isOffScreen{false};
    QRect absoluteGeometry;
    QRect localGeometry;
    QRect screenGeometry;
    int strutsThickness{0};
    QRect publishedStruts;
    QRect maskRect;
    QRect inputMask;
    bool editMode{false};
    bool inConfigureAppletsMode{false};
};

//! Enum-to-string names for the JSON fields. Exhaustive switches with no
//! default, so adding an enum value trips -Wswitch right here instead of
//! silently serializing something stale; Q_UNREACHABLE() documents that
//! every value returns above.

inline QString viewTypeName(Types::ViewType type)
{
    switch (type) {
    case Types::DockView: return QStringLiteral("dock");
    case Types::PanelView: return QStringLiteral("panel");
    }

    Q_UNREACHABLE();
}

inline QString edgeName(Plasma::Types::Location location)
{
    switch (location) {
    case Plasma::Types::Floating: return QStringLiteral("floating");
    case Plasma::Types::Desktop: return QStringLiteral("desktop");
    case Plasma::Types::FullScreen: return QStringLiteral("fullscreen");
    case Plasma::Types::TopEdge: return QStringLiteral("top");
    case Plasma::Types::BottomEdge: return QStringLiteral("bottom");
    case Plasma::Types::LeftEdge: return QStringLiteral("left");
    case Plasma::Types::RightEdge: return QStringLiteral("right");
    }

    Q_UNREACHABLE();
}

inline QString alignmentName(Types::Alignment alignment)
{
    switch (alignment) {
    case Types::NoneAlignment: return QStringLiteral("none");
    case Types::Center: return QStringLiteral("center");
    case Types::Left: return QStringLiteral("left");
    case Types::Right: return QStringLiteral("right");
    case Types::Top: return QStringLiteral("top");
    case Types::Bottom: return QStringLiteral("bottom");
    case Types::Justify: return QStringLiteral("justify");
    }

    Q_UNREACHABLE();
}

inline QString visibilityModeName(Types::Visibility mode)
{
    switch (mode) {
    case Types::None: return QStringLiteral("none");
    case Types::AlwaysVisible: return QStringLiteral("alwaysVisible");
    case Types::AutoHide: return QStringLiteral("autoHide");
    case Types::DodgeActive: return QStringLiteral("dodgeActive");
    case Types::DodgeMaximized: return QStringLiteral("dodgeMaximized");
    case Types::DodgeAllWindows: return QStringLiteral("dodgeAllWindows");
    case Types::WindowsGoBelow: return QStringLiteral("windowsGoBelow");
    case Types::WindowsCanCover: return QStringLiteral("windowsCanCover");
    case Types::WindowsAlwaysCover: return QStringLiteral("windowsAlwaysCover");
    case Types::SidebarOnDemand: return QStringLiteral("sidebarOnDemand");
    case Types::SidebarAutoHide: return QStringLiteral("sidebarAutoHide");
    case Types::NormalWindow: return QStringLiteral("normalWindow");
    }

    Q_UNREACHABLE();
}

inline QJsonArray serializeRect(const QRect &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

inline QJsonObject serializeViewRecord(const ViewRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(record.containmentId);
    json[QStringLiteral("layout")] = record.layout;
    json[QStringLiteral("isCloned")] = record.isCloned;
    json[QStringLiteral("isClonedFrom")] = record.isClonedFrom;
    json[QStringLiteral("type")] = viewTypeName(record.type);
    json[QStringLiteral("screen")] = record.screen;
    json[QStringLiteral("onPrimary")] = record.onPrimary;
    json[QStringLiteral("edge")] = edgeName(record.edge);
    json[QStringLiteral("alignment")] = alignmentName(record.alignment);
    json[QStringLiteral("visibilityMode")] = visibilityModeName(record.visibilityMode);
    json[QStringLiteral("isHidden")] = record.isHidden;
    json[QStringLiteral("inStartup")] = record.inStartup;
    json[QStringLiteral("isOffScreen")] = record.isOffScreen;
    json[QStringLiteral("absoluteGeometry")] = serializeRect(record.absoluteGeometry);
    json[QStringLiteral("localGeometry")] = serializeRect(record.localGeometry);
    json[QStringLiteral("screenGeometry")] = serializeRect(record.screenGeometry);
    json[QStringLiteral("strutsThickness")] = record.strutsThickness;
    json[QStringLiteral("publishedStruts")] = serializeRect(record.publishedStruts);
    json[QStringLiteral("maskRect")] = serializeRect(record.maskRect);

    //! the input region is a single rect today (Effects::inputMask), but the
    //! interface doc specifies an array of rects so multi-rect input regions
    //! can land later without a schema break. An invalid or empty rect means
    //! "no input restriction published" (Effects::setInputMask clears the
    //! window mask for those), reported as an empty array.
    QJsonArray inputRegion;

    if (record.inputMask.isValid() && !record.inputMask.isEmpty()) {
        inputRegion.append(serializeRect(record.inputMask));
    }

    json[QStringLiteral("inputRegionRects")] = inputRegion;
    json[QStringLiteral("editMode")] = record.editMode;
    json[QStringLiteral("inConfigureAppletsMode")] = record.inConfigureAppletsMode;

    return json;
}

inline QString serializeViewRecords(const QList<ViewRecord> &records)
{
    QJsonArray array;

    for (const auto &record : records) {
        array.append(serializeViewRecord(record));
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

//! snapshot one live view into a value record (dbusreports.cpp)
ViewRecord collectViewRecord(const Latte::View *view, bool inConfigureAppletsMode);

//! serialize a set of live views for the viewsData() D-Bus read
QString collectViewsData(const QList<Latte::View *> &views, bool inConfigureAppletsMode);

}
}

#endif
