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
}
