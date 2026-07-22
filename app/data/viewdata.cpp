/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "viewdata.h"

namespace Latte {
namespace Data {

const int View::ISCLONEDNULL = -1;

View::View()
    : Generic()
{
}

View::View(View &&o)
    : Generic(o),
      isActive(o.isActive),
      isMoveOrigin(o.isMoveOrigin),
      isMoveDestination(o.isMoveDestination),
      onPrimary(o.onPrimary),
      isClonedFrom(o.isClonedFrom),
      screen(o.screen),
      screenEdgeMargin(o.screenEdgeMargin),
      screensGroup(o.screensGroup),
      linkPlacement(o.linkPlacement),
      maxLength(o.maxLength),
      edge(o.edge),
      alignment(o.alignment),
      m_state(o.m_state),
      m_originFile(o.m_originFile),
      m_originLayout(o.m_originLayout),
      m_originView(o.m_originView),
      errors(o.errors),
      warnings(o.warnings),
      subcontainments(o.subcontainments)
{
}

View::View(const View &o)
    : Generic(o),
      isActive(o.isActive),
      isMoveOrigin(o.isMoveOrigin),
      isMoveDestination(o.isMoveDestination),
      onPrimary(o.onPrimary),
      isClonedFrom(o.isClonedFrom),
      screen(o.screen),
      screenEdgeMargin(o.screenEdgeMargin),
      screensGroup(o.screensGroup),
      linkPlacement(o.linkPlacement),
      maxLength(o.maxLength),
      edge(o.edge),
      alignment(o.alignment),
      m_state(o.m_state),
      m_originFile(o.m_originFile),
      m_originLayout(o.m_originLayout),
      m_originView(o.m_originView),
      errors(o.errors),
      warnings(o.warnings),
      subcontainments(o.subcontainments)
{
}

View::View(const QString &newid, const QString &newname)
    : Generic(newid, newname)
{
}

View &View::operator=(const View &rhs)
{
    id = rhs.id;
    name = rhs.name;
    isActive = rhs.isActive;
    isMoveOrigin = rhs.isMoveOrigin;
    isMoveDestination = rhs.isMoveDestination;
    onPrimary = rhs.onPrimary;
    isClonedFrom = rhs.isClonedFrom;
    screen = rhs.screen;
    screenEdgeMargin = rhs.screenEdgeMargin;
    screensGroup = rhs.screensGroup;
    linkPlacement = rhs.linkPlacement;
    maxLength = rhs.maxLength;
    edge = rhs.edge;
    alignment = rhs.alignment;
    m_state = rhs.m_state;
    m_originFile = rhs.m_originFile;
    m_originLayout = rhs.m_originLayout;
    m_originView = rhs.m_originView;
    errors = rhs.errors;
    warnings = rhs.warnings;

    subcontainments = rhs.subcontainments;

    return (*this);
}

View &View::operator=(View &&rhs)
{
    id = rhs.id;
    name = rhs.name;
    isActive = rhs.isActive;
    isMoveOrigin = rhs.isMoveOrigin;
    isMoveDestination = rhs.isMoveDestination;
    onPrimary = rhs.onPrimary;
    isClonedFrom = rhs.isClonedFrom;
    screen = rhs.screen;
    screenEdgeMargin = rhs.screenEdgeMargin;
    screensGroup = rhs.screensGroup;
    linkPlacement = rhs.linkPlacement;
    maxLength = rhs.maxLength;
    edge = rhs.edge;
    alignment = rhs.alignment;
    m_state = rhs.m_state;
    m_originFile = rhs.m_originFile;
    m_originLayout = rhs.m_originLayout;
    m_originView = rhs.m_originView;
    errors = rhs.errors;
    warnings = rhs.warnings;

    subcontainments = rhs.subcontainments;

    return (*this);
}

bool View::operator==(const View &rhs) const
{
    return (id == rhs.id)
            && (name == rhs.name)
            //&& (isActive == rhs.isActive) /*Disabled because this is not needed in order to track view changes for saving*/
            //&& (isMoveOrigin == rhs.isMoveOrigin) /*Disabled because this is not needed in order to track view changes for saving*/
            //&& (isMoveDestination == rhs.isMoveDestination) /*Disabled because this is not needed in order to track view changes for saving*/
            && (onPrimary == rhs.onPrimary)
            && (isClonedFrom == rhs.isClonedFrom)
            && (screen == rhs.screen)
            && (screenEdgeMargin == rhs.screenEdgeMargin)
            && (screensGroup == rhs.screensGroup)
            && (linkPlacement == rhs.linkPlacement)
            && (maxLength == rhs.maxLength)
            && (edge == rhs.edge)
            && (alignment == rhs.alignment)
            && (m_state == rhs.m_state)
            && (m_originFile == rhs.m_originFile)
            && (m_originLayout == rhs.m_originLayout)
            && (m_originView == rhs.m_originView)
            //&& (errors == rhs.errors) /*Disabled because this is not needed in order to track view changes for saving*/
            //&& (warnings == rhs.warnings) /*Disabled because this is not needed in order to track view changes for saving*/
            && (subcontainments == rhs.subcontainments);
}

bool View::operator!=(const View &rhs) const
{
    return !(*this == rhs);
}

View::operator QString() const
{
    QString result;

    result += id;
    result +=" : ";
    result += isActive ? "Active" : "Inactive";
    result +=" : ";
    //! the combined check must come first: either single-flag branch also
    //! matches when both flags are set, which made this branch dead code
    //! and hid the both-directions marker
    if (m_state==OriginFromLayout && isMoveOrigin && isMoveDestination) {
        result += " ↑↓ ";
    } else if (m_state==OriginFromLayout && isMoveOrigin) {
        result += " ↑ ";
    } else if (m_state==OriginFromLayout && isMoveDestination) {
        result += " ↓ ";
    } else {
        result += " - ";
    }

    result += " : ";

    if (m_state == IsInvalid) {
        result += "IsInvalid";
    } else if (m_state == IsCreated) {
        result += "IsCreated";
    } else if (m_state == OriginFromViewTemplate) {
        result += "OriginFromViewTemplate";
    } else if (m_state == OriginFromLayout) {
        result += "OriginFromLayout";
    }

    result += " : ";
    if (isCloned()) {
        //! this was `"Cloned from:" + isClonedFrom`: const char* plus int is
        //! POINTER arithmetic, printing a garbage suffix of the literal (and
        //! undefined behavior once the id exceeds the literal's length)
        result += QStringLiteral("Cloned from:") + QString::number(isClonedFrom);
    } else {
        result += "Original";
    }

    result += " : ";
    if (screensGroup == Latte::Types::SingleScreenGroup) {
        result += onPrimary ? "Primary" : "Explicit";
    } else if (screensGroup == Latte::Types::AllScreensGroup) {
        result += "All Screens";
    } else if (screensGroup == Latte::Types::AllSecondaryScreensGroup) {
        result += "All Secondary Screens";
    }
    //! the single-screen branch above already printed Primary/Explicit; an
    //! unconditional second append here doubled it ("PrimaryPrimary") and
    //! stamped a stray Primary/Explicit onto the multi-screen groups

    result += " : ";
    result += QString::number(screen);
    result += " : ";
    if (edge == Plasma::Types::BottomEdge) {
        result += "BottomEdge";
    } else if (edge == Plasma::Types::TopEdge) {
        result += "TopEdge";
    } else if (edge == Plasma::Types::LeftEdge) {
        result += "LeftEdge";
    } else if (edge == Plasma::Types::RightEdge) {
        result += "RightEdge";
    }

    result += " : ";

    if (alignment == Latte::Types::Center) {
        result += "CenterAlignment";
    } else if (alignment == Latte::Types::Left) {
        result += "LeftAlignment";
    } else if (alignment == Latte::Types::Right) {
        result += "RightAlignment";
    } else if (alignment == Latte::Types::Top) {
        result += "TopAlignment";
    } else if (alignment == Latte::Types::Bottom) {
        result += "BottomAlignment";
    } else if (alignment == Latte::Types::Justify) {
        result += "JustifyAlignment";
    }

    result += " : ";
    result += QString::number(maxLength) + "%";

    result += " || ";
    result += "{" + QString(subcontainments) + "}";

    return result;
}

bool View::isCreated() const
{
    return m_state == IsCreated;
}

bool View::isOriginal() const
{
    return !isCloned();
}

bool View::isCloned() const
{
    return isClonedFrom != ISCLONEDNULL;
}

bool View::isScreenGroupReplica() const
{
    return isCloned() && linkPlacement == LinkPlacement::ScreenGroupDerived;
}

bool View::isExplicitlyLinked() const
{
    return isCloned() && linkPlacement == LinkPlacement::ExplicitTarget;
}

bool View::hasValidLinkPlacement() const
{
    switch (linkPlacement) {
    case LinkPlacement::ScreenGroupDerived:
    case LinkPlacement::ExplicitTarget:
        return true;
    }

    return false;
}

bool View::isValid() const
{
    return m_state != IsInvalid;
}

bool View::isHorizontal() const
{
    return !isVertical();
}

bool View::isVertical() const
{
    return (edge == Plasma::Types::LeftEdge || edge == Plasma::Types::RightEdge);
}

View View::toIndependentSnapshot() const
{
    View snapshot{*this};
    snapshot.isClonedFrom = ISCLONEDNULL;
    snapshot.screensGroup = Latte::Types::SingleScreenGroup;
    snapshot.linkPlacement = LinkPlacement::ScreenGroupDerived;
    snapshot.isMoveOrigin = false;
    snapshot.isMoveDestination = false;
    return snapshot;
}

View View::toExplicitLinkedMember(const int relationshipRootId,
                                  const int targetScreenId,
                                  const Plasma::Types::Location targetEdge) const
{
    Q_ASSERT(relationshipRootId > 0);
    Q_ASSERT(targetScreenId >= Latte::ScreenPool::FIRSTSCREENID);
    Q_ASSERT(targetEdge == Plasma::Types::TopEdge
             || targetEdge == Plasma::Types::BottomEdge
             || targetEdge == Plasma::Types::LeftEdge
             || targetEdge == Plasma::Types::RightEdge);

    View linked{*this};
    linked.isClonedFrom = relationshipRootId;
    linked.screensGroup = Latte::Types::SingleScreenGroup;
    linked.linkPlacement = LinkPlacement::ExplicitTarget;
    linked.onPrimary = false;
    linked.screen = targetScreenId;
    linked.edge = targetEdge;
    linked.alignment = normalizeAlignmentForEdge(alignment, targetEdge);
    return linked;
}

bool View::hasViewTemplateOrigin() const
{
    return m_state == OriginFromViewTemplate;
}

bool View::hasLayoutOrigin() const
{
    return m_state == OriginFromLayout;
}

bool View::hasSubContainment(const QString &subId) const
{
    return subcontainments.containsId(subId);
}

QString View::originFile() const
{
    return m_originFile;
}

QString View::originLayout() const
{
    return m_originLayout;
}

QString View::originView() const
{
    return m_originView;
}

View::State View::state() const
{
    return m_state;
}

void View::setState(View::State state, QString file, QString layout, QString view)
{
    m_state = state;
    m_originFile = file;
    m_originLayout = layout;
    m_originView = view;
}

}
}
