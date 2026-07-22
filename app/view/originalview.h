/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ORIGINALVIEW_H
#define ORIGINALVIEW_H

// local
#include <coretypes.h>
#include "view.h"

// Qt
#include <QList>

namespace Latte {

class ClonedView;

class OriginalView : public View
{
    Q_OBJECT
    Q_PROPERTY(Latte::Types::ScreensGroup screensGroup READ screensGroup NOTIFY screensGroupChanged)

public:
    OriginalView(Plasma::Corona *corona, QScreen *targetScreen = nullptr, bool byPassX11WM = false);
    ~OriginalView();

    bool isOriginal() const override;
    bool isCloned() const override;
    bool isSingle() const override;

    int clonesCount() const;
    int explicitLinkedMembersCount() const;

    [[nodiscard]] bool canRemove() const override;

    int expectedScreenIdFromScreenGroup(const Latte::Types::ScreensGroup &nextScreensGroup) const;

    Latte::Types::ScreensGroup screensGroup() const override;
    Latte::Data::View::LinkPlacement linkPlacement() const override;
    void setScreensGroup(const Latte::Types::ScreensGroup &group);

    void addClone(Latte::ClonedView *view);
    void setNextLocationForClones(const QString layoutName, int edge, int alignment);
    [[nodiscard]] bool addApplet(const QString &pluginId) override;
    [[nodiscard]] bool removeApplet(int appletId) override;
    void synchronizeDroppedApplet(QObject *mimeData, int x, int y) override;
    void addApplet(QObject *mimedata, int x, int y, uint excludedContainmentId);

    void reconsiderScreen() override;

public Q_SLOTS:
    void cleanClones();

Q_SIGNALS:
    void screensGroupChanged();    

private Q_SLOTS:    
    void syncClonesToScreens();

    void restoreConfig();
    void saveConfig();

private:
    void cleanScreenGroupClones();
    void createClone(int screenId);
    void forgetClone(Latte::ClonedView *view);
    void removeClone(Latte::ClonedView *view);

private:
    Latte::Types::ScreensGroup m_screensGroup{Latte::Types::SingleScreenGroup};
    QList<QPointer<Latte::ClonedView>> m_clones;

    QList<int> m_waitingCreation;

    friend class Latte::ClonedView;
};

}

#endif
