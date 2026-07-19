/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.containment 0.1 as LatteContainment

import "./layouter" as LayouterElements

Item {
    id: _fillsPrivate

    property Item layouts: null
    property Item animations: null
    property Item indexer: null

    readonly property int fillApplets: startLayout.fillApplets + mainLayout.fillApplets + endLayout.fillApplets
    readonly property int shownApplets: startLayout.shownApplets + mainLayout.shownApplets + endLayout.shownApplets
    readonly property int sizeWithNoFillApplets: startLayout.sizeWithNoFillApplets + mainLayout.sizeWithNoFillApplets + endLayout.sizeWithNoFillApplets

    readonly property int maxLength: root.myView.alignment === LatteCore.Types.Justify ? contentsMaxLength : Math.min(root.minLength, contentsMaxLength)

    readonly property int contentsMaxLength: root.maxLength - background.totals.paddingsLength

    readonly property Item startLayout: LayouterElements.AppletsContainer {
        grid: _fillsPrivate.layouts.startLayout
    }

    readonly property Item mainLayout: LayouterElements.AppletsContainer {
        grid: _fillsPrivate.layouts.mainLayout
    }

    readonly property Item endLayout: LayouterElements.AppletsContainer {
        grid: _fillsPrivate.layouts.endLayout
    }

    onFillAppletsChanged: layouter.updateSizeForAppletsInFill();
    onShownAppletsChanged: layouter.updateSizeForAppletsInFill();
    onSizeWithNoFillAppletsChanged: layouter.updateSizeForAppletsInFill();

    //!         FILLWIDTH/FILLHEIGHT COMPUTATIONS
    //! The two-pass arithmetic that sizes fillWidth/fillHeight applets
    //! lives in the containment plugin's FillLengthDistributor core
    //! (EX-05 in docs/tracking/QML_EXTRACTION_PLAN.md); this file keeps only the
    //! live-item boundary - gathering the constraint records the passes
    //! consume and applying the lengths they produce.

    //! one grid child as the distributor's FillItem record. The metric
    //! and fill-length properties exist only on real AppletItems (edge
    //! spacers and placeholders carry the flags alone) and the passes
    //! only ever read them for autofill items, so others are recorded
    //! absent
    function _fillItemRecordFor(item: Item) : var {
        var autoFill = item.isAutoFillApplet === true;
        return {
            "autoFill": autoFill,
            "hidden": item.isHidden === true,
            "hasApplet": item.applet ? true : false,
            "splitter": item.isInternalViewSplitter === true,
            "min": autoFill ? item.appletMinimumLength : -1,
            "pref": autoFill ? item.appletPreferredLength : -1,
            "max": autoFill ? item.appletMaximumLength : -1,
            "liveMax": autoFill ? item.maxAutoFillLength : -1,
            "liveMin": autoFill ? item.minAutoFillLength : -1
        };
    }

    //! the distributor's LayoutSnapshot: the per-item records plus the
    //! aggregates the sibling counter component keeps computing (they
    //! are the reactive update triggers and have other consumers, so
    //! they stay QML and travel as inputs)
    function _gatherLayoutForFillsDistribution(container: Item) : var {
        var grid = container.grid;
        var items = [];
        for (var i = 0; i < grid.children.length; ++i) {
            items.push(_fillItemRecordFor(grid.children[i]));
        }
        return {
            "items": items,
            "sizeWithNoFill": container.sizeWithNoFillApplets,
            "fillApplets": container.fillApplets,
            "shownApplets": container.shownApplets,
            "gridLength": grid.length
        };
    }

    //! entries carry max/min keys only for what a pass actually
    //! assigned; untouched items keep their previous values exactly as
    //! the in-place passes left them unwritten
    function _applyFillAssignments(grid: Item, assigned: var) {
        for (var i = 0; i < assigned.length; ++i) {
            var entry = assigned[i];
            if (entry.max !== undefined) {
                grid.children[i].maxAutoFillLength = entry.max;
            }
            if (entry.min !== undefined) {
                grid.children[i].minAutoFillLength = entry.min;
            }
        }
    }

    function _updateSizeForAppletsInFill() {
        if (!inNormalFillCalculationsState) {
            return;
        }

        if (startLayout.fillApplets + mainLayout.fillApplets + endLayout.fillApplets === 0) {
            return;
        }

        //! One consistent snapshot serves both distribution passes:
        //! positioner geometry (grid.length) and the counter aggregates
        //! only update at polish, never inside this synchronous call, so
        //! gathering everything first observes exactly what the old
        //! in-place passes read mid-run.
        var result = LatteContainment.FillDistributor.distribute(
                    _gatherLayoutForFillsDistribution(startLayout),
                    _gatherLayoutForFillsDistribution(mainLayout),
                    _gatherLayoutForFillsDistribution(endLayout),
                    root.myView.alignment === LatteCore.Types.Justify,
                    root.minLength,
                    contentsMaxLength);

        if (result.start === undefined) {
            //! the wrapper refused a malformed snapshot and already said why
            return;
        }

        _applyFillAssignments(startLayout.grid, result.start);
        _applyFillAssignments(mainLayout.grid, result.main);
        _applyFillAssignments(endLayout.grid, result.end);
    }
}
