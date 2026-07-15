/*
    SPDX-License-Identifier: GPL-2.0-or-later

    Qt 6 type-semantics contracts. Seed test for the contract suite - see
    README.md in this directory for the rules.
*/

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2

TestCase {
    id: root
    name: "Qt6TypeContracts"
    when: windowShown

    //! Contract: a JS array assigned to a QQC2 control's model round-trips
    //! back as a QVariantList, NOT a JS array - Array.isArray() is false on
    //! the read-back value, and role lookups resolve through modelData only.
    //! Depended on by: declarativeimports/components ComboBox popup delegate
    //! (role resolution), earned in a302d742 where the Qt5-era
    //! Array.isArray branch selected the wrong lookup for every array model
    //! and rendered collapsed rows with invisible text.
    Component {
        id: comboComponent
        ComboBox {
            model: ["alpha", "beta"]
        }
    }

    function test_jsArrayModelReadsBackAsVariantList() {
        var combo = createTemporaryObject(comboComponent, root);
        verify(combo);
        compare(combo.count, 2);

        //! the read-back model must not satisfy Array.isArray: the engine
        //! converted it to QVariantList crossing the C++ property
        verify(!Array.isArray(combo.model),
               "QQC2 model no longer converts JS arrays to QVariantList - "
               + "revisit the ComboBox role-resolution fix (a302d742)");

        //! but it must still be usable as a sequence from JS
        compare(combo.model.length, 2);
        compare(combo.model[0], "alpha");
    }

    //! Contract: properties of a plain QtObject declared as 'var' keep JS
    //! array identity - Array.isArray stays true. This is the boundary that
    //! makes the previous contract surprising: conversion happens at typed
    //! C++ property crossings, not everywhere.
    QtObject {
        id: holder
        property var arr: ["a"]
    }

    function test_varPropertyKeepsJsArrayIdentity() {
        verify(Array.isArray(holder.arr),
               "var properties no longer preserve JS arrays - every "
               + "Array.isArray check in the tree needs a re-audit");
    }

    //! Contract: Qt6 removed the Qt.MidButton alias; only Qt.MiddleButton
    //! exists, and the removed name reads back as undefined instead of
    //! erroring, so a comparison against it is silently always-false.
    //! Depended on by: every middle-click site (TaskMouseArea,
    //! ClickedAnimation, EnvironmentActions), earned in 81f0093e where all
    //! middle-click actions were dead because of exactly that silent false.
    function test_midButtonAliasIsGone() {
        verify(Qt.MiddleButton !== undefined,
               "Qt.MiddleButton disappeared - middle-click handling broke at the enum level");
        verify(Qt.MidButton === undefined,
               "Qt.MidButton exists again - the 81f0093e renames are no longer "
               + "load-bearing and comparisons against it may be intentional or stale");
    }

    //! Contract: Qt6 QQC2 AbstractButton carries its OWN 'indicator'
    //! property (it lived on CheckBox/RadioButton/Switch subclasses when the
    //! Qt5-era code was written), and QML scope resolution prefers the scope
    //! object's properties, so a bare 'indicator' inside ANY button scope
    //! resolves to the control's glyph item, not to an outer or context
    //! property of that name. Depended on by: the indicator config packages,
    //! which receive an 'indicator' context property from the config API and
    //! must alias it (latteIndicator) before use inside control scopes -
    //! earned in 33fa17d7 (CheckBox) and the follow-up Button fix in the
    //! default indicator's style/glow buttons.
    property QtObject outerIndicator: QtObject { objectName: "outerIndicator" }

    Component {
        id: shadowProbeComponent
        Item {
            property alias probeButton: shadowedButton
            //! plays the role the config API's context property plays: an
            //! outer 'indicator' name a bare reference SHOULD find if the
            //! control did not shadow it
            property QtObject indicator: root.outerIndicator

            Button {
                id: shadowedButton
                function bareIndicatorInScope() { return indicator; }
            }
        }
    }

    function test_abstractButtonShadowsOuterIndicatorName() {
        var wrapper = createTemporaryObject(shadowProbeComponent, root);
        verify(wrapper);
        compare(wrapper.indicator, root.outerIndicator);

        var bare = wrapper.probeButton.bareIndicatorInScope();
        verify(bare !== root.outerIndicator,
               "bare 'indicator' inside a Button scope reached the outer property - "
               + "AbstractButton lost its own 'indicator' and the latteIndicator "
               + "aliases in the indicator configs are no longer load-bearing");
        compare(bare, wrapper.probeButton.indicator);
    }

    //! Contract: assigning a fractional value to an int-typed QML property
    //! discards the fraction (ECMAScript ToInt32: truncation toward zero).
    //! This is what turned wayland's fractional pointer coordinates into
    //! cursor-vs-widget drift when a per-event delta accumulator stored its
    //! positions as int (36160c46), and it is the basis on which the
    //! remaining 'property int' pointer caches were judged safe: their
    //! bounded sub-1px error never accumulates.
    QtObject {
        id: intHolder
        property int stored
    }

    function test_intPropertyDiscardsFraction() {
        intHolder.stored = 5.7;
        compare(intHolder.stored, 5,
                "int property no longer truncates 5.7 to 5 - re-derive the "
                + "error bounds used to audit pointer-coordinate caches");

        intHolder.stored = -5.7;
        compare(intHolder.stored, -5,
                "int property no longer truncates -5.7 toward zero");
    }
}
