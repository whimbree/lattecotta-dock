/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COLORIZERDECIDER_H
#define COLORIZERDECIDER_H

// local
#include "units/colorizerdecision.h"

// Qt
#include <QObject>
#include <QPointer>

namespace Latte {
namespace Containment {

//! Thin QML shell over the ColorizerDecision pure core (EX-12 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/colorizerdecision.h). The colorizer
//! Manager.qml instantiates one decider, binds the environment facts and
//! the candidate SchemeColors objects as inputs, and reads the resolved
//! decisions back. All DECISIONS live in the core; this class only
//! snapshots its inputs into a plain env and maps the core's enums onto
//! the held objects.
//!
//! Candidate objects are held as QPointer: Theme rebuilds its SchemeColors
//! with deleteLater on scheme changes (app/plasma/extended/theme.cpp), and
//! the QML rebind on themeChanged races that destruction - a raw pointer
//! window here would hand QML a dangling object.
class ColorizerDecider : public QObject
{
    Q_OBJECT

    //! inputs: candidate palette objects (SchemeColors instances; typed as
    //! QObject because the containment plugin cannot link app/wm)
    Q_PROPERTY(QObject *plasmaTheme MEMBER m_plasmaTheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *darkTheme MEMBER m_darkTheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *lightTheme MEMBER m_lightTheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *layoutScheme MEMBER m_layoutScheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *selectedActiveWindowScheme MEMBER m_selectedActiveWindowScheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *currentScreenActiveWindowScheme MEMBER m_currentScreenActiveWindowScheme NOTIFY inputsChanged)
    Q_PROPERTY(QObject *touchingWindowScheme MEMBER m_touchingWindowScheme NOTIFY inputsChanged)

    //! inputs: settings (validated at the boundary - an unknown value is
    //! refused loudly and renders as plasma default)
    Q_PROPERTY(int themeColors READ themeColors WRITE setThemeColors NOTIFY inputsChanged)
    Q_PROPERTY(int windowColors READ windowColors WRITE setWindowColors NOTIFY inputsChanged)

    //! inputs: environment facts (field-for-field the core's ColorizerEnv)
    Q_PROPERTY(bool graphicsSystemAccelerated MEMBER m_graphicsSystemAccelerated NOTIFY inputsChanged)
    Q_PROPERTY(bool compositingActive MEMBER m_compositingActive NOTIFY inputsChanged)
    Q_PROPERTY(bool themeExtendedExists MEMBER m_themeExtendedExists NOTIFY inputsChanged)
    Q_PROPERTY(bool plasmaThemeIsLight MEMBER m_plasmaThemeIsLight NOTIFY inputsChanged)
    Q_PROPERTY(bool windowsTrackerEnabled MEMBER m_windowsTrackerEnabled NOTIFY inputsChanged)
    Q_PROPERTY(bool existsWindowTouching MEMBER m_existsWindowTouching NOTIFY inputsChanged)
    Q_PROPERTY(bool existsWindowTouchingEdge MEMBER m_existsWindowTouchingEdge NOTIFY inputsChanged)
    Q_PROPERTY(bool activeWindowTouching MEMBER m_activeWindowTouching NOTIFY inputsChanged)
    Q_PROPERTY(bool activeWindowTouchingEdge MEMBER m_activeWindowTouchingEdge NOTIFY inputsChanged)
    Q_PROPERTY(bool layoutExists MEMBER m_layoutExists NOTIFY inputsChanged)
    Q_PROPERTY(bool plasmaBackgroundForPopups MEMBER m_plasmaBackgroundForPopups NOTIFY inputsChanged)
    Q_PROPERTY(bool hasExpandedApplet MEMBER m_hasExpandedApplet NOTIFY inputsChanged)
    Q_PROPERTY(bool userShowPanelBackground MEMBER m_userShowPanelBackground NOTIFY inputsChanged)
    Q_PROPERTY(bool plasmaStyleBusyForTouchingBusyVerticalView MEMBER m_plasmaStyleBusyForTouchingBusyVerticalView NOTIFY inputsChanged)
    Q_PROPERTY(bool forceSolidPanel MEMBER m_forceSolidPanel NOTIFY inputsChanged)
    Q_PROPERTY(bool forcePanelForBusyBackground MEMBER m_forcePanelForBusyBackground NOTIFY inputsChanged)
    Q_PROPERTY(bool inConfigureAppletsMode MEMBER m_inConfigureAppletsMode NOTIFY inputsChanged)
    Q_PROPERTY(bool editModeTextColorIsBright MEMBER m_editModeTextColorIsBright NOTIFY inputsChanged)
    Q_PROPERTY(double currentBackgroundBrightness MEMBER m_currentBackgroundBrightness NOTIFY inputsChanged)
    Q_PROPERTY(double backgroundStoredOpacity MEMBER m_backgroundStoredOpacity NOTIFY inputsChanged)

    //! outputs
    Q_PROPERTY(QObject *applyTheme READ applyTheme NOTIFY decisionChanged)
    Q_PROPERTY(QObject *schemeColors READ schemeColors NOTIFY decisionChanged)
    Q_PROPERTY(bool mustBeShown READ mustBeShown NOTIFY decisionChanged)
    Q_PROPERTY(bool useLayoutTextColor READ useLayoutTextColor NOTIFY decisionChanged)

public:
    explicit ColorizerDecider(QObject *parent = nullptr);

    int themeColors() const;
    void setThemeColors(int themeColors);

    int windowColors() const;
    void setWindowColors(int windowColors);

    //! the scheme object the colorizer applies (Manager.qml's applyTheme)
    QObject *applyTheme() const;
    //! the object whose schemeFile the colorizer publishes; null means the
    //! "kdeglobals" literal (the shell maps it)
    QObject *schemeColors() const;
    bool mustBeShown() const;
    bool useLayoutTextColor() const;

Q_SIGNALS:
    void inputsChanged();
    void decisionChanged();

private:
    void recomputeDecision();
    ColorizerDecision::ColorizerEnv snapshotEnv() const;
    QObject *resolveThemeObject(ColorizerDecision::ThemeSource source) const;
    QObject *resolveSchemeFileObject(ColorizerDecision::SchemeFile file, QObject *applied) const;
    void logEditModeBreadcrumb(const ColorizerDecision::ColorizerEnv &env,
                               ColorizerDecision::SchemeFile file) const;

    //! inputs
    QPointer<QObject> m_plasmaTheme;
    QPointer<QObject> m_darkTheme;
    QPointer<QObject> m_lightTheme;
    QPointer<QObject> m_layoutScheme;
    QPointer<QObject> m_selectedActiveWindowScheme;
    QPointer<QObject> m_currentScreenActiveWindowScheme;
    QPointer<QObject> m_touchingWindowScheme;

    int m_themeColors{Types::PlasmaThemeColors};
    int m_windowColors{Types::NoneWindowColors};

    bool m_graphicsSystemAccelerated{true};
    bool m_compositingActive{true};
    bool m_themeExtendedExists{false};
    bool m_plasmaThemeIsLight{false};
    bool m_windowsTrackerEnabled{false};
    bool m_existsWindowTouching{false};
    bool m_existsWindowTouchingEdge{false};
    bool m_activeWindowTouching{false};
    bool m_activeWindowTouchingEdge{false};
    bool m_layoutExists{false};
    bool m_plasmaBackgroundForPopups{false};
    bool m_hasExpandedApplet{false};
    bool m_userShowPanelBackground{false};
    bool m_plasmaStyleBusyForTouchingBusyVerticalView{false};
    bool m_forceSolidPanel{false};
    bool m_forcePanelForBusyBackground{false};
    bool m_inConfigureAppletsMode{false};
    bool m_editModeTextColorIsBright{false};
    double m_currentBackgroundBrightness{-1000.0};
    double m_backgroundStoredOpacity{1.0};

    //! outputs (QPointer: a held decision must not outlive its object)
    QPointer<QObject> m_applyTheme;
    QPointer<QObject> m_schemeColors;
    bool m_mustBeShown{false};
    bool m_useLayoutTextColor{false};
};

}
}

#endif
