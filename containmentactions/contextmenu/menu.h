/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MENU_H
#define MENU_H

// Qt
#include <QHash>
#include <QObject>

// Plasma
#include <Plasma/ContainmentActions>

class QAction;
class QMenu;

enum ViewType
{
    DockView = 0,
    PanelView
};

struct LayoutInfo {
    QString layoutName;
    bool isBackgroundFileIcon;
    QString iconName;
};

struct ViewTypeData {
    ViewType type{ViewType::DockView};
    bool isCloned{true};
    int clonesCount{0};
    int explicitLinkedMembersCount{0};
    bool isExplicitlyLinked{false};
};

class Menu : public Plasma::ContainmentActions
{
    Q_OBJECT

public:
    Menu(QObject *parent, const QVariantList &args);
    ~Menu() override;

    QList<QAction *> contextualActions() override;
    void restore(const KConfigGroup &) override;

    QAction *action(const QString &name);
private Q_SLOTS:
    void populateLayouts();
    void populateMoveToLayouts();
    void populateViewTemplates();
    void populateLinkedViewTargets(QMenu *menu);
    void quitApplication();
    void requestConfiguration();
    void requestWidgetExplorer();
    bool updateViewData();
    void updateVisibleActions();

    void addView(QAction *action);
    void moveToLayout(QAction *action);
    void switchToLayout(QAction *action);

private:
    QStringList m_data;
    QStringList m_viewTemplates;
    QString m_screensData;

    QStringList m_actionsAlwaysShown;
    QStringList m_activeLayoutNames;

    ViewTypeData m_view;
    bool m_contextDataValid{false};

    QHash<QString, QAction *> m_actions;

    QMenu *m_addViewMenu{nullptr};
    QMenu *m_switchLayoutsMenu{nullptr};
    QMenu *m_moveToLayoutMenu{nullptr};
};

#endif
