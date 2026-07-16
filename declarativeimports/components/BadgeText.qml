/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.2

import org.kde.plasma.plasmoid 2.0
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore

Rectangle {
    id: badgeRoot

    property double proportion: 0

    property double previousProportion: 0

    property bool style3d: true

    property int numberValue
    property string textValue

    property bool fullCircle: true
    property bool showNumber: true
    property bool showText: false
    property bool textWithBackgroundColor: false

    property int radiusPerCentage: 100
    property int minimumWidth: 0
    property int maximumWidth: 9999

    property double circleOpacity: 1
    property double fontPixelSize: badgeRoot.partSize // * 0.55

    // ring geometry from the BadgeMath core (EX-20): outer radius is half
    // the height (a transient negative height during creation clamps to an
    // empty ring), inner radius hollows the full circle into a 90% ring
    property double stdThickness: LatteCore.BadgeMath.ringOuterRadius(badgeRoot.height)
    property double circleThicknessAttr: LatteCore.BadgeMath.ringInnerRadius(badgeRoot.height, badgeRoot.fullCircle)
    property double partSize: badgeRoot.height / 2

    width: Math.max(badgeRoot.minimumWidth, valueText.width + 4*Kirigami.Units.smallSpacing)

    color: Kirigami.Theme.backgroundColor
    radius: (badgeRoot.radiusPerCentage / 100) * (badgeRoot.height / 2)
    border.width: 0 //Math.max(1,width/64)

    property int borderWidth: 1
    property real borderOpacity: 1
    property color borderColor: Kirigami.Theme.textColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightedColor: Kirigami.Theme.focusColor

    readonly property bool singleCharacter: (badgeRoot.showNumber && badgeRoot.numberValue<=9 && badgeRoot.numberValue>=0)
                                            || (badgeRoot.showText && badgeRoot.textValue.length===1)

    onProportionChanged: {
        if (badgeRoot.proportion<0.03) {
            badgeRoot.previousProportion = 0;
        }

        //console.log(previousProportion + " - "+proportion);
        var currentStep = (badgeRoot.proportion - badgeRoot.previousProportion);
        if ((currentStep >= 0.01) || (badgeRoot.proportion>=1 && badgeRoot.previousProportion !==1)) {
            //   console.log("request repaint...");
            badgeRoot.previousProportion = badgeRoot.proportion;
            badgeRoot.repaint();
        }
    }

    function repaint() : void {
        canvas.requestPaint()
    }

    Canvas {
        id: canvas

        property int lineWidth: 1
        property bool fill: true
        property bool stroke: true
        property real alpha: 1.0

        // edge bleeding fix
        readonly property double filler: 0.01

        width: parent.width - 2 * parent.borderWidth
        height: parent.height - 2 * parent.borderWidth
        opacity: badgeRoot.proportion > 0 ? 1 : 0

        anchors.centerIn: parent

        property color drawColor: badgeRoot.highlightedColor

        onDrawColorChanged: canvas.requestPaint();

        onPaint: {
            var ctx = canvas.getContext('2d');
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.fillStyle = canvas.drawColor;

            // arc geometry from the BadgeMath core (EX-20): twelve o'clock
            // start, clockwise sweep; the filler stays here because it is a
            // Canvas edge-bleed workaround, not geometry
            var startRadian = LatteCore.BadgeMath.arcStartRadian();
            var sweepRadian = LatteCore.BadgeMath.arcSweepRadian(badgeRoot.proportion);

            ctx.beginPath();
            ctx.arc(canvas.width/2, canvas.height/2, badgeRoot.stdThickness,
                    startRadian, startRadian + sweepRadian + canvas.filler, false);
            ctx.arc(canvas.width/2, canvas.height/2, badgeRoot.circleThicknessAttr,
                    startRadian + sweepRadian + canvas.filler, startRadian, true);

            ctx.closePath();
            ctx.fill();
        }
    }

    Rectangle {
        id: badgerBackground
        anchors.fill: canvas
        color: canvas.drawColor

        visible: badgeRoot.proportion === 1 && badgeRoot.showNumber
        radius: parent.radius
    }

    Text {
        id: valueText
        objectName: "badgeLabelText" // the e2e layer reads the shown label through this
        anchors.centerIn: canvas

        width: Math.min(badgeRoot.maximumWidth - 4*Kirigami.Units.smallSpacing, valueText.implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        elide: Text.ElideRight

        text: {
            // count label (locale grouping, the 9,999+ clamp) from the
            // BadgeMath core; an empty label falls through to text mode
            // exactly like the Qt5 branch structure did
            if (badgeRoot.showNumber) {
                var numberLabel = LatteCore.BadgeMath.countLabel(badgeRoot.numberValue);
                if (numberLabel !== "") {
                    return numberLabel;
                }
            }

            if (badgeRoot.showText) {
                return badgeRoot.textValue;
            }

            return "";
        }
        font.pixelSize: 0.62 * parent.height
        font.bold: true
        color: badgeRoot.textWithBackgroundColor ? parent.color : parent.textColor
        visible: badgeRoot.showNumber || badgeRoot.showText
    }

    Rectangle{
        anchors.fill: parent
        anchors.topMargin: parent.borderWidth
        anchors.bottomMargin: parent.borderWidth
        anchors.leftMargin: parent.borderWidth
        anchors.rightMargin: parent.borderWidth
        color: "transparent"
        border.width: parent.borderWidth > 0 ? parent.borderWidth+1 : 0
        border.color: "black"
        radius: parent.radius
        opacity: 0.4

        visible: badgeRoot.style3d
    }

    Rectangle{
        anchors.fill: parent
        border.width: parent.borderWidth
        border.color: {
            if (badgeRoot.style3d) {
                return parent.borderColor
            }

            return badgeRoot.proportion === 1 ? parent.highlightedColor : parent.color
        }
        color: "transparent"
        radius: parent.radius
        opacity: parent.borderOpacity
    }
}
