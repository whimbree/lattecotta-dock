/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls 2.15 as QQC2
import QtQuick.Layouts 1.3

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.components 3.0 as PlasmaComponents3

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.private.containment 0.1 as LatteContainment
import org.kde.kirigami 2.20 as Kirigami

PlasmaComponents.Page {
    id: page
    width: content.width + content.Layout.leftMargin * 2
    height: content.height + Kirigami.Units.smallSpacing * 2

    TextMetrics {
        id: defaultFontMetrics
        text: "M"
        font: Kirigami.Theme.defaultFont
    }

    Timer {
        id: syncGeometry

        running: false
        repeat: false
        interval: 400
        onTriggered: viewConfig.syncGeometry()
    }

    ColumnLayout {
        id: content

        width: (dialog.appliedWidth - Kirigami.Units.smallSpacing * 2) - Layout.leftMargin * 2
        spacing: dialog.subGroupSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        Layout.leftMargin: Kirigami.Units.smallSpacing * 2

        //! BEGIN: Items
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing

            spacing: Kirigami.Units.smallSpacing

            LatteComponents.Header {
                text: i18n("Items")
            }

            ColumnLayout {
                Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                spacing: 0

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing
                    enabled: proportionSizeSlider.value === 1

                    PlasmaComponents.Label {
                        text: i18nc("absolute size","Absolute size")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: appletsSizeSlider
                        Layout.fillWidth: true
                        //!round to nearest odd number
                        value: 2 * Math.round(plasmoid.configuration.iconSize / 2)
                        from: 16
                        to: 512
                        stepSize: dialog.advancedLevel || (plasmoid.configuration.iconSize % 8 !== 0) || dialog.viewIsPanel ? 2 : 8
                        wheelEnabled: false

                        function updateIconSize() {
                            if (!pressed) {
                                plasmoid.configuration.iconSize = value;
                                syncGeometry.restart();
                            }
                        }

                        onPressedChanged: {
                            updateIconSize()
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updateIconSize);
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updateIconSize);
                        }
                    }

                    PlasmaComponents.Label {
                        text: i18nc("number in pixels, e.g. 12 px.", "%1 px.", appletsSizeSlider.value)//.arg(appletsSizeSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing
                    visible: dialog.advancedLevel || plasmoid.configuration.proportionIconSize>0

                    PlasmaComponents.Label {
                        text: i18nc("relative size", "Relative size")
                        horizontalAlignment: Text.AlignLeft
                        enabled: proportionSizeSlider.value !== proportionSizeSlider.from
                    }

                    LatteComponents.Slider {
                        id: proportionSizeSlider
                        Layout.fillWidth: true
                        value: plasmoid.configuration.proportionIconSize
                        from: 1.0
                        to: (latteView.visibility.mode === LatteCore.Types.SidebarOnDemand || latteView.visibility.mode === LatteCore.Types.SidebarAutoHide)  ? 25 : 12
                        stepSize: 0.1
                        wheelEnabled: false

                        function updateProportionIconSize() {
                            if (!pressed) {
                                if(value===1) {
                                    plasmoid.configuration.proportionIconSize = -1;
                                } else {
                                    plasmoid.configuration.proportionIconSize = value;
                                }
                            }
                        }

                        onPressedChanged: {
                            updateProportionIconSize();
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updateProportionIconSize)
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updateProportionIconSize)
                        }
                    }

                    PlasmaComponents.Label {
                        id: absoluteSizeLbl
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        text: proportionSizeSlider.value !== proportionSizeSlider.from ?
                                  (absoluteSizeLblMouseArea.containsMouse ?
                                       i18nc("number in pixels, e.g. 64 px.","%1 px.", latteView.metrics.maxIconSize) :
                                       i18nc("number in percentage, e.g. 85 %","%1 %", proportionSizeSlider.value.toFixed(1))) :
                                  i18nc("no value in percentage","--- %")
                        horizontalAlignment: Text.AlignRight
                        enabled: proportionSizeSlider.value !== proportionSizeSlider.from

                        MouseArea {
                            id: absoluteSizeLblMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing
                    enabled: LatteCore.WindowSystem.compositingActive && plasmoid.configuration.animationsEnabled

                    PlasmaComponents.Label {
                        text: i18n("Zoom on hover")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: zoomSlider
                        Layout.fillWidth: true
                        value: Number(1 + plasmoid.configuration.zoomLevel / 20).toFixed(2)
                        from: 1
                        to: 2.25
                        stepSize: 0.05
                        wheelEnabled: false

                        function updateZoomLevel() {
                            if (!pressed) {
                                var result = Math.round((value - 1) * 20)
                                plasmoid.configuration.zoomLevel = result
                            }
                        }

                        onPressedChanged: {
                            updateZoomLevel()
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updateZoomLevel)
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updateZoomLevel)
                        }
                    }

                    PlasmaComponents.Label {
                        text: i18nc("number in percentage, e.g. 85 %","%1 %", Number((zoomSlider.value * 100) - 100).toFixed(0))
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }
            }
        }
        //! END: Items

        //! BEGIN: Length
        ColumnLayout {
            Layout.fillWidth: true

            spacing: Kirigami.Units.smallSpacing

            LatteComponents.Header {
                text: i18n("Length")
            }

            ColumnLayout {
                id: lengthColumn
                Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                spacing: 0

                readonly property int labelsMaxWidth: Math.max(maxLengthLbl.implicitWidth,
                                                               minLengthLbl.implicitWidth,
                                                               offsetLbl.implicitWidth)

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents.Label {
                        id: maxLengthLbl
                        text: i18n("Maximum")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: maxLengthSlider
                        Layout.fillWidth: true

                        from: 0
                        to: 100
                        stepSize: 1
                        wheelEnabled: false

                        readonly property int localMinValue: 1

                        //! config drives the handle through a proxy property, NOT a
                        //! declarative `value: plasmoid.configuration.maxLength` binding
                        //! (D16): a plain value binding is DESTROYED by the first
                        //! imperative assignment (the drag-snap below, or a handle drag),
                        //! and nothing re-establishes it - after that first touch the
                        //! canvas ruler and any external config edit no longer moved the
                        //! handle, so the ruler and the slider disagreed. The proxy is
                        //! never assigned imperatively, so its binding survives; a change
                        //! to it re-syncs the handle. Same pattern the offset slider
                        //! already uses (offsetValue + updateOffset), generalized here.
                        property real maxLengthValue: plasmoid.configuration.maxLength

                        //! delegates to the shared clamp authority (EX-18,
                        //! app/settings/lengthoffsetclamp.h); the canvas maxLength ruler
                        //! drives the same core, so the two can no longer drift apart.
                        //! The Qt5 body ended with a minLengthSlider.updateMinLength()
                        //! re-trigger that was dead since Qt5: the clamp floors maxLength
                        //! by minLength, so maxLength < minLength cannot hold afterwards
                        //! (toValue_maxNeverEndsBelowMin pins the proof).
                        function updateMaxLength() {
                            if (!pressed && viewConfig.isReady) {
                                var clamped = lengthClamp.clampMaxLengthToValue(value,
                                                                                plasmoid.configuration.minLength,
                                                                                plasmoid.configuration.offset,
                                                                                plasmoid.configuration.alignment);
                                plasmoid.configuration.maxLength = clamped.maxLength;

                                if (plasmoid.configuration.offset !== clamped.offset) {
                                    plasmoid.configuration.offset = clamped.offset;
                                }
                            } else {
                                //! view-side snap while dragging; the config math lives in the core.
                                //! Justify has no independent minimum (its Minimum slider is
                                //! disabled below), so its drag floor is localMinValue only -
                                //! flooring by a frozen leftover minLength would strand the handle
                                //! above an un-editable floor (D17, matching the core's
                                //! maximumIsFlooredByMinimum). Non-Justify keeps the min floor,
                                //! bit-for-bit the old `(value < minLength) || (value < localMinValue)`.
                                var effectiveMin = (plasmoid.configuration.alignment === LatteCore.Types.Justify)
                                                   ? localMinValue
                                                   : Math.max(plasmoid.configuration.minLength, localMinValue);
                                if (value < effectiveMin) {
                                    value = effectiveMin;
                                }
                            }
                        }

                        //! re-establish the handle from config after any interaction
                        //! clobbered it (D16); skipped while pressed so a live drag owns
                        //! the value
                        function resyncMaxLengthHandle() {
                            if (!pressed) {
                                value = maxLengthValue;
                            }
                        }

                        onPressedChanged: {
                            updateMaxLength();
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updateMaxLength);
                            maxLengthValueChanged.connect(resyncMaxLengthHandle);
                            resyncMaxLengthHandle();
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updateMaxLength);
                            maxLengthValueChanged.disconnect(resyncMaxLengthHandle);
                        }
                    }

                    PlasmaComponents.Label {
                        text: i18nc("number in percentage, e.g. 85 %","%1 %", maxLengthSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        LatteComponents.ScrollArea {
                            anchors.fill: parent
                            delayIsEnabled: false

                            readonly property real smallStep: 0.1

                            onScrolledUp: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.maxLength = plasmoid.configuration.maxLength + smallStep;
                                }
                            }

                            onScrolledDown: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.maxLength = plasmoid.configuration.maxLength - smallStep;
                                }
                            }

                            onClicked: {
                                plasmoid.configuration.maxLength = Math.round(plasmoid.configuration.maxLength);
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing
                    visible: dialog.advancedLevel
                    enabled: (plasmoid.configuration.alignment !== LatteCore.Types.Justify)

                    PlasmaComponents.Label {
                        id: minLengthLbl
                        text: i18n("Minimum")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: minLengthSlider
                        Layout.fillWidth: true

                        from: 0
                        to: 100
                        stepSize: 1
                        wheelEnabled: false

                        //! config drives the handle through a proxy property, not a
                        //! declarative `value:` binding, so a drag cannot clobber the
                        //! ruler / external re-sync (D16 - same pattern as maxLengthSlider
                        //! and the offset slider)
                        property real minLengthValue: plasmoid.configuration.minLength

                        function updateMinLength() {
                            if (!pressed  && viewConfig.isReady) {
                                plasmoid.configuration.minLength = value; //Math.min(value, plasmoid.configuration.maxLength);

                                if (plasmoid.configuration.minLength > maxLengthSlider.value) {
                                    maxLengthSlider.updateMaxLength();
                                }
                            } else {
                                if (value > plasmoid.configuration.maxLength) {
                                    value = plasmoid.configuration.maxLength
                                }
                            }
                        }

                        //! re-establish the handle from config after any interaction
                        //! clobbered it (D16); skipped while pressed so a live drag owns
                        //! the value
                        function resyncMinLengthHandle() {
                            if (!pressed) {
                                value = minLengthValue;
                            }
                        }

                        onPressedChanged: {
                            updateMinLength();
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updateMinLength);
                            minLengthValueChanged.connect(resyncMinLengthHandle);
                            resyncMinLengthHandle();
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updateMinLength);
                            minLengthValueChanged.disconnect(resyncMinLengthHandle);
                        }
                    }

                    PlasmaComponents.Label {
                        text: i18nc("number in percentage, e.g. 85 %","%1 %", minLengthSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        LatteComponents.ScrollArea {
                            anchors.fill: parent
                            delayIsEnabled: false

                            readonly property real smallStep: 0.1

                            onScrolledUp: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.minLength = plasmoid.configuration.minLength + smallStep;
                                }
                            }

                            onScrolledDown: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.minLength = plasmoid.configuration.minLength - smallStep;
                                }
                            }

                            onClicked: {
                                plasmoid.configuration.minLength = Math.round(plasmoid.configuration.minLength);
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing
                    visible: dialog.advancedLevel
                    enabled: offsetSlider.to > offsetSlider.from

                    PlasmaComponents.Label {
                        id: offsetLbl
                        text: i18n("Offset")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: offsetSlider
                        Layout.fillWidth: true
                        stepSize: 1
                        wheelEnabled: false

                        //! these properties are used in order to not update view_offset incorrectly when the primary config view
                        //! is changing between different views
                        property bool userInputIsValid: false
                        readonly property bool sliderIsReady: viewConfig.isReady && (from===fromValue) && (to===toValue)

                        //! the reachable range comes from the shared clamp authority
                        //! (EX-18); the int property keeps the shipped truncation of
                        //! fractional bounds, which the core reproduces
                        readonly property int fromValue: lengthClamp.offsetSliderFrom(plasmoid.configuration.maxLength,
                                                                                      plasmoid.configuration.alignment)

                        readonly property int toValue: lengthClamp.offsetSliderTo(plasmoid.configuration.maxLength,
                                                                                  plasmoid.configuration.alignment)

                        property real offsetValue: plasmoid.configuration.offset

                        Binding {
                            target: offsetSlider
                            property: "from"
                            when: viewConfig.isReady
                            restoreMode: Binding.RestoreNone
                            value: offsetSlider.fromValue
                        }

                        Binding {
                            target: offsetSlider
                            property: "to"
                            when: viewConfig.isReady
                            restoreMode: Binding.RestoreNone
                            value: offsetSlider.toValue
                        }

                        //! delegates to the shared clamp authority (EX-18): while the
                        //! handle is being dragged the slider owns the value, otherwise
                        //! the config is the source and gets bounded into the range
                        function updateOffset() {
                            if (!pressed && sliderIsReady) {
                                var requested = userInputIsValid ? value : plasmoid.configuration.offset;
                                var clamped = lengthClamp.clampOffset(plasmoid.configuration.maxLength,
                                                                      requested,
                                                                      plasmoid.configuration.alignment);
                                value = clamped.offset;
                                plasmoid.configuration.offset = clamped.offset;

                                if (plasmoid.configuration.maxLength !== clamped.maxLength) {
                                    plasmoid.configuration.maxLength = clamped.maxLength;
                                }
                            }
                        }

                        onPressedChanged: {
                            if (pressed) {
                                userInputIsValid = true;
                            } else {
                                updateOffset();
                                userInputIsValid = false;
                            }
                        }

                        Component.onCompleted: {
                            offsetValueChanged.connect(updateOffset);
                            fromChanged.connect(updateOffset);
                            toChanged.connect(updateOffset);
                            sliderIsReadyChanged.connect(updateOffset);

                            updateOffset();
                        }

                        Component.onDestruction: {
                            offsetValueChanged.disconnect(updateOffset);
                            fromChanged.disconnect(updateOffset);
                            toChanged.disconnect(updateOffset);
                            sliderIsReadyChanged.disconnect(updateOffset);
                        }
                    }

                    PlasmaComponents.Label {
                        text: i18nc("number in percentage, e.g. 85 %","%1 %", offsetSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        LatteComponents.ScrollArea {
                            anchors.fill: parent
                            delayIsEnabled: false

                            readonly property real smallStep: 0.1

                            onScrolledUp: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.offset= plasmoid.configuration.offset + smallStep;
                                }
                            }

                            onScrolledDown: (wheel) => {
                                var ctrlModifier = (wheel.modifiers & Qt.ControlModifier);
                                if (ctrlModifier) {
                                    plasmoid.configuration.offset = plasmoid.configuration.offset - smallStep;
                                }
                            }

                            onClicked: {
                                plasmoid.configuration.offset = Math.round(plasmoid.configuration.offset);
                            }
                        }
                    }
                }
            }
            LatteComponents.SubHeader {
                visible: dialog.advancedLevel
                text: i18nc("@label dynamic length configuration", "Dynamic Length Adjustments")
                enabled: true
            }

            LatteComponents.CheckBoxesColumn {
                enabled: dialog.advancedLevel;
                LatteComponents.CheckBox {
                    id: maximizeWhenMaximizedChk
                    Layout.maximumWidth: dialog.optionsWidth
                    text: i18nc("@label", "Maximize panel length in presence of maximized windows")
                    tooltip: i18n("Change panel length to maximum screen size when there is a maximized window present on the screen")
                    enabled: showBackground.checked
                    visible: dialog.advancedLevel
                    value: plasmoid.configuration.maximizeWhenMaximized

                    onClicked: {
                        plasmoid.configuration.maximizeWhenMaximized = !plasmoid.configuration.maximizeWhenMaximized;
                    }
                }
            }
        }
        //! END: Length

        //! BEGIN: Margins
        ColumnLayout {
            id: marginsColumn
            Layout.fillWidth: true

            spacing: Kirigami.Units.smallSpacing
            visible: dialog.advancedLevel

            readonly property int maxMargin: 25

            LatteComponents.Header {
                text: i18n("Margins")
            }

            ColumnLayout{
                Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                spacing: 0

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth

                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents.Label {
                        text: i18n("Length")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: lengthExtMarginSlider
                        Layout.fillWidth: true

                        value: plasmoid.configuration.lengthExtMargin
                        from: 0
                        to: marginsColumn.maxMargin
                        stepSize: 1
                        wheelEnabled: false

                        onPressedChanged: {
                            if (!pressed) {
                                plasmoid.configuration.lengthExtMargin = value;
                            }
                        }
                    }

                    PlasmaComponents.Label {
                        text: lengthMarginLblMouseArea.containsMouse ?
                                  i18nc("number in pixels, e.g. 8 px.","%1 px.", currentValueInPixels) :
                                  i18nc("number in percentage, e.g. 85 %","%1 %", lengthExtMarginSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        readonly property int currentValueInPixels: (lengthExtMarginSlider.value/100) * latteView.metrics.maxIconSize

                        MouseArea {
                            id: lengthMarginLblMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents.Label {
                        text: i18n("Thickness")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: thickMarginSlider
                        Layout.fillWidth: true

                        value: plasmoid.configuration.thickMargin
                        from: 0
                        to: 60
                        stepSize: 1
                        wheelEnabled: false
                        minimumInternalValue: latteView.indicator.info.minThicknessPadding * 100

                        onPressedChanged: {
                            if (!pressed) {
                                plasmoid.configuration.thickMargin = value;
                            }
                        }
                    }

                    PlasmaComponents.Label {
                        text: thickMarginLblMouseArea.containsMouse ?
                                  i18nc("number in pixels, e.g. 8 px.","%1 px.", currentValueInPixels) :
                                  i18nc("number in percentage, e.g. 85 %","%1 %", currentValue)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        readonly property int currentValue: Math.max(thickMarginSlider.minimumInternalValue, thickMarginSlider.value)
                        readonly property int currentValueInPixels: (currentValue/100) * latteView.metrics.maxIconSize

                        MouseArea {
                            id: thickMarginLblMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents.Label {
                        text: i18n("Floating gap")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: screenEdgeMarginSlider
                        Layout.fillWidth: true

                        value: plasmoid.configuration.screenEdgeMargin
                        from: -1
                        to: 256
                        stepSize: 1
                        wheelEnabled: false

                        onPressedChanged: {
                            if (!pressed) {
                                plasmoid.configuration.screenEdgeMargin = value;
                            }
                        }
                    }

                    PlasmaComponents.Label {
                        text: currentValue < 0 ? "---" : i18nc("number in pixels, e.g. 85 px.","%1 px.", currentValue)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4

                        readonly property int currentValue: screenEdgeMarginSlider.value
                    }
                }
            }
        }
        //! END: Margins

        //! BEGIN: Colors
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            visible: dialog.advancedLevel

            LatteComponents.Header {
                Layout.columnSpan: 4
                text: i18n("Colors")
            }

            GridLayout {
                id: colorsGridLayout
                Layout.minimumWidth: dialog.optionsWidth
                Layout.maximumWidth: Layout.minimumWidth
                Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                Layout.topMargin: Kirigami.Units.smallSpacing
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                columns: 2

                readonly property bool colorsScriptIsPresent: universalSettings.colorsScriptIsPresent

                PlasmaComponents.Label {
                    text: i18n("Palette")
                }

                LatteComponents.ComboBox {
                    Layout.fillWidth: true
                    model: [
                        {
                            name: i18nc("plasma theme colors", "Plasma Theme Colors"),
                            value: LatteContainment.Types.PlasmaThemeColors
                        },{
                            name: i18nc("dark theme colors", "Dark Colors"),
                            value: LatteContainment.Types.DarkThemeColors
                        },{
                            name: i18nc("light theme colors", "Light Colors"),
                            value: LatteContainment.Types.LightThemeColors
                        },{
                            name: i18nc("layout custom colors", "Layout Custom Colors"),
                            value: LatteContainment.Types.LayoutThemeColors
                        },{
                            //! D23: Reverse is a real themeColors value (types.h,
                            //! main.xml) but was left out of this model while
                            //! colorsToIndex still mapped it, colliding with
                            //! Layout on index 3 - so a Reverse config showed
                            //! (and, via onCurrentIndexChanged, was rewritten to)
                            //! Layout. Restore it as its own row so every value
                            //! the dropdown can hold maps to a distinct index.
                            name: i18nc("reverse plasma theme colors", "Reverse"),
                            value: LatteContainment.Types.ReverseThemeColors
                        },{
                            name: i18nc("smart theme colors", "Smart Colors Based On Desktop Background"),
                            value: LatteContainment.Types.SmartThemeColors
                        }
                    ]

                    currentIndex: colorsToIndex(plasmoid.configuration.themeColors)
                    textRole: "name"
                    onCurrentIndexChanged: plasmoid.configuration.themeColors = model[currentIndex].value

                    //! D23: distinct index per value, in model-row order
                    //! (Plasma0/Dark1/Light2/Layout3/Reverse4/Smart5). Reverse and
                    //! Layout used to both return 3 while Reverse was missing from
                    //! the model, so a Reverse config showed as Layout.
                    function colorsToIndex(colors) {
                        if (colors === LatteContainment.Types.PlasmaThemeColors) {
                            return 0;
                        } else if (colors === LatteContainment.Types.DarkThemeColors) {
                            return 1;
                        } else if (colors === LatteContainment.Types.LightThemeColors) {
                            return 2;
                        } else if (colors === LatteContainment.Types.LayoutThemeColors) {
                            return 3;
                        } else if (colors === LatteContainment.Types.ReverseThemeColors) {
                            return 4;
                        } else if (colors === LatteContainment.Types.SmartThemeColors) {
                            return 5;
                        }
                    }
                }

                PlasmaComponents.Label {
                    text: i18n("From Window")
                }

                LatteComponents.ComboBox {
                    Layout.fillWidth: true
                    model: [
                        {
                            name:i18n("Disabled"),
                            icon: "",
                            toolTip: "Colors are not going to take into account any windows"
                        },{
                            name:i18n("Current Active Window"),
                            icon: !colorsGridLayout.colorsScriptIsPresent ? "state-warning" : "",
                            toolTip: colorsGridLayout.colorsScriptIsPresent ?
                                         i18n("Colors are going to be based on the active window") :
                                         i18n("Colors are going to be based on the active window.\nWarning: You need to install Colors KWin Script from KDE Store")
                        },{
                            name: i18n("Any Touching Window"),
                            icon: !colorsGridLayout.colorsScriptIsPresent ? "state-warning" : "",
                            toolTip: colorsGridLayout.colorsScriptIsPresent ?
                                         i18n("Colors are going to be based on windows that are touching the view") :
                                         i18n("Colors are going to be based on windows that are touching the view.\nWarning: You need to install Colors KWin Script from KDE Store")
                        }
                    ]


                    textRole: "name"
                    iconRole: "icon"
                    toolTipRole: "toolTip"
                    blankSpaceForEmptyIcons: !colorsGridLayout.colorsScriptIsPresent
                    popUpAlignRight: Qt.application.layoutDirection !== Qt.RightToLeft

                    currentIndex: plasmoid.configuration.windowColors
                    onCurrentIndexChanged: plasmoid.configuration.windowColors = currentIndex
                }
            }
        }
        //! END: Colors

        //! BEGIN: Background
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            LatteComponents.HeaderSwitch {
                id: showBackground
                Layout.minimumWidth: dialog.optionsWidth + 2 *Kirigami.Units.smallSpacing
                Layout.maximumWidth: Layout.minimumWidth
                Layout.minimumHeight: implicitHeight
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                enabled: LatteCore.WindowSystem.compositingActive

                checked: plasmoid.configuration.useThemePanel
                text: i18n("Background")
                tooltip: i18n("Enable/disable background")

                onPressed: {
                    plasmoid.configuration.useThemePanel = !plasmoid.configuration.useThemePanel;
                }
            }

            ColumnLayout {
                Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                spacing: 0

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    enabled: LatteCore.WindowSystem.compositingActive

                    PlasmaComponents.Label {
                        enabled: showBackground.checked
                        text: i18n("Thickness")
                        horizontalAlignment: Text.AlignLeft
                    }

                    LatteComponents.Slider {
                        id: panelSizeSlider
                        Layout.fillWidth: true
                        enabled: showBackground.checked

                        value: plasmoid.configuration.panelSize
                        from: 0
                        to: 100
                        stepSize: 1
                        wheelEnabled: false

                        function updatePanelSize() {
                            if (!pressed)
                                plasmoid.configuration.panelSize = value
                        }

                        onPressedChanged: {
                            updatePanelSize();
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updatePanelSize)
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updatePanelSize)
                        }
                    }

                    PlasmaComponents.Label {
                        enabled: showBackground.checked
                        text: i18nc("number in percentage, e.g. 85 %","%1 %", panelSizeSlider.value)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    enabled: LatteCore.WindowSystem.compositingActive

                    PlasmaComponents.Label {
                        text: plasmoid.configuration.backgroundOnlyOnMaximized && plasmoid.configuration.solidBackgroundForMaximized ?
                                  i18nc("opacity when desktop background is busy from contrast point of view","Busy Opacity") : i18n("Opacity")
                        horizontalAlignment: Text.AlignLeft
                        enabled: transparencySlider.enabled
                    }

                    LatteComponents.Slider {
                        id: transparencySlider
                        Layout.fillWidth: true
                        enabled: showBackground.checked //&& !blockOpacityAdjustment

                        value: plasmoid.configuration.panelTransparency
                        from: -1
                        to: 100
                        stepSize: 1
                        wheelEnabled: false

                        function updatePanelTransparency() {
                            if (!pressed)
                                plasmoid.configuration.panelTransparency = value
                        }

                        onPressedChanged: {
                            updatePanelTransparency();
                        }

                        Component.onCompleted: {
                            valueChanged.connect(updatePanelTransparency);
                        }

                        Component.onDestruction: {
                            valueChanged.disconnect(updatePanelTransparency);
                        }
                    }

                    PlasmaComponents.Label {
                        enabled: transparencySlider.enabled
                        text: transparencySlider.value >= 0 ? i18nc("number in percentage, e.g. 85 %","%1 %", transparencySlider.value) : i18nc("Default word abbreviation", "Def.")
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    visible: dialog.advancedLevel && dialog.kirigamiLibraryIsFound

                    PlasmaComponents.Label {
                        text: i18n("Radius")
                        horizontalAlignment: Text.AlignLeft
                        enabled: radiusSlider.enabled
                    }

                    LatteComponents.Slider {
                        id: radiusSlider
                        Layout.fillWidth: true
                        enabled: showBackground.checked

                        value: plasmoid.configuration.backgroundRadius
                        from: -1
                        to: 50
                        stepSize: 1
                        wheelEnabled: false

                        function updateBackgroundRadius() {
                            if (!pressed) {
                                plasmoid.configuration.backgroundRadius = value
                            }
                        }

                        onPressedChanged: updateBackgroundRadius();
                        Component.onCompleted: valueChanged.connect(updateBackgroundRadius);
                        Component.onDestruction: valueChanged.disconnect(updateBackgroundRadius);
                    }

                    PlasmaComponents.Label {
                        enabled: radiusSlider.enabled
                        text: radiusSlider.value >= 0 ? i18nc("number in percentage, e.g. 85 %","%1 %", radiusSlider.value) : i18nc("Default word abbreviation", "Def.")
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    enabled: LatteCore.WindowSystem.compositingActive
                    visible: dialog.advancedLevel && dialog.kirigamiLibraryIsFound

                    PlasmaComponents.Label {
                        text: i18n("Shadow")
                        horizontalAlignment: Text.AlignLeft
                        enabled: shadowSlider.enabled
                    }

                    LatteComponents.Slider {
                        id: shadowSlider
                        Layout.fillWidth: true
                        enabled: showBackground.checked && panelShadows.checked

                        value: plasmoid.configuration.backgroundShadowSize
                        from: -1
                        to: 50
                        stepSize: 1
                        wheelEnabled: false

                        function updateBackgroundShadowSize() {
                            if (!pressed) {
                                plasmoid.configuration.backgroundShadowSize = value
                            }
                        }

                        onPressedChanged: updateBackgroundShadowSize();
                        Component.onCompleted: valueChanged.connect(updateBackgroundShadowSize);
                        Component.onDestruction: valueChanged.disconnect(updateBackgroundShadowSize);
                    }

                    PlasmaComponents.Label {
                        enabled: shadowSlider.enabled
                        text: shadowSlider.value >= 0 ? i18nc("number in pixels, e.g. 12 px.", "%1 px.", shadowSlider.value ) : i18nc("Default word abbreviation", "Def.")
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: defaultFontMetrics.advanceWidth * 4
                        Layout.maximumWidth: defaultFontMetrics.advanceWidth * 4
                    }
                }

                RowLayout {
                    Layout.minimumWidth: dialog.optionsWidth
                    Layout.maximumWidth: Layout.minimumWidth
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    spacing: 2
                    visible: dialog.advancedLevel

                    readonly property int buttonSize: (dialog.optionsWidth - (2 * spacing)) / children.length

                    PlasmaComponents.Button {
                        id: panelBlur
                        Layout.minimumWidth: parent.buttonSize
                        Layout.maximumWidth: Layout.minimumWidth
                        text: i18n("Blur")
                        checkable: true
                        enabled: showBackground.checked && LatteCore.WindowSystem.compositingActive
                        QQC2.ToolTip.text: i18n("Background is blurred underneath")
                        QQC2.ToolTip.visible: hovered

                        readonly property int blurEnabled: plasmoid.configuration.blurEnabled

                        onBlurEnabledChanged: checked = blurEnabled;

                        onClicked: {
                            plasmoid.configuration.blurEnabled  = checked
                        }
                    }

                    PlasmaComponents.Button {
                        id: panelShadows
                        Layout.minimumWidth: parent.buttonSize
                        Layout.maximumWidth: Layout.minimumWidth
                        text: i18n("Shadows")
                        checkable: true
                        enabled: showBackground.checked && LatteCore.WindowSystem.compositingActive && themeExtended.hasShadow
                        QQC2.ToolTip.text: i18n("Background shows its shadows")
                        QQC2.ToolTip.visible: hovered

                        readonly property int panelShadows: plasmoid.configuration.panelShadows

                        onPanelShadowsChanged: checked = panelShadows

                        onClicked: {
                            plasmoid.configuration.panelShadows  = checked
                        }
                    }

                    PlasmaComponents.Button {
                        id: solidBackground
                        Layout.minimumWidth: parent.buttonSize
                        Layout.maximumWidth: Layout.minimumWidth
                        text: i18n("Outline")
                        checkable: true
                        checked: plasmoid.configuration.panelOutline
                        enabled: showBackground.checked
                        QQC2.ToolTip.text: i18n("Background draws a line for its borders. You can set the line size from Latte Preferences")
                        QQC2.ToolTip.visible: hovered

                        onClicked: {
                            plasmoid.configuration.panelOutline = checked;
                        }
                    }

                    PlasmaComponents.Button {
                        id: allCorners
                        Layout.minimumWidth: parent.buttonSize
                        Layout.maximumWidth: Layout.minimumWidth
                        text: i18n("All Corners")
                        checkable: true
                        checked: plasmoid.configuration.backgroundAllCorners
                        enabled: showBackground.checked
                                 && ((plasmoid.configuration.screenEdgeMargin===-1) /*no-floating*/
                                     || (plasmoid.configuration.screenEdgeMargin > -1 /*floating with justify alignment and 100% maxlength*/
                                         && plasmoid.configuration.alignment ===LatteCore.Types.Justify
                                         && plasmoid.configuration.maxLength===100))
                        QQC2.ToolTip.text: i18n("Background draws all corners at all cases.")
                        QQC2.ToolTip.visible: hovered

                        onClicked: {
                            plasmoid.configuration.backgroundAllCorners = checked;
                        }
                    }
                }

                LatteComponents.SubHeader {
                    visible: dialog.advancedLevel
                    text: i18nc("dynamic visibility for background", "Dynamic Visibility")
                    enabled: LatteCore.WindowSystem.compositingActive
                }

                LatteComponents.CheckBoxesColumn {
                    enabled: LatteCore.WindowSystem.compositingActive
                    LatteComponents.CheckBox {
                        id: solidForMaximizedChk
                        Layout.maximumWidth: dialog.optionsWidth
                        text: i18n("Prefer opaque background when touching any window")
                        tooltip: i18n("Background removes its transparency setting when a window is touching")
                        enabled: showBackground.checked
                        visible: dialog.advancedLevel
                        value: plasmoid.configuration.solidBackgroundForMaximized

                        onClicked: {
                            plasmoid.configuration.solidBackgroundForMaximized = !plasmoid.configuration.solidBackgroundForMaximized;
                        }
                    }

                    LatteComponents.CheckBox {
                        id: onlyOnMaximizedChk
                        Layout.maximumWidth: dialog.optionsWidth
                        text: i18n("Hide background when not needed")
                        tooltip: i18n("Background becomes hidden except when a window is touching or the desktop background is busy")
                        enabled: showBackground.checked
                        visible: dialog.advancedLevel
                        value: plasmoid.configuration.backgroundOnlyOnMaximized

                        onClicked: {
                            plasmoid.configuration.backgroundOnlyOnMaximized = !plasmoid.configuration.backgroundOnlyOnMaximized;
                        }
                    }

                    LatteComponents.CheckBox {
                        id: hideShadowsOnMaximizedChk
                        Layout.maximumWidth: dialog.optionsWidth
                        text: i18n("Hide background shadow for maximized windows")
                        tooltip: i18n("Background shadows become hidden when an active maximized window is touching the view")
                        enabled: showBackground.checked
                        visible: dialog.advancedLevel
                        value: plasmoid.configuration.disablePanelShadowForMaximized

                        onClicked: {
                            plasmoid.configuration.disablePanelShadowForMaximized = !plasmoid.configuration.disablePanelShadowForMaximized;
                        }
                    }
                }

                LatteComponents.SubHeader {
                    visible: dialog.advancedLevel
                    text: i18n("Exceptions")
                    enabled: LatteCore.WindowSystem.compositingActive
                }

                LatteComponents.CheckBox {
                    id: solidForPopupsChk
                    Layout.maximumWidth: dialog.optionsWidth
                    text: i18n("Prefer Plasma background and colors for expanded applets")
                    tooltip: i18n("Background becomes opaque in plasma style when applets are expanded")
                    enabled: showBackground.checked
                    visible: dialog.advancedLevel
                    value: plasmoid.configuration.plasmaBackgroundForPopups

                    onClicked: {
                        plasmoid.configuration.plasmaBackgroundForPopups = !plasmoid.configuration.plasmaBackgroundForPopups;
                    }
                }
            }
        }
        //! END: Background
    }
}
