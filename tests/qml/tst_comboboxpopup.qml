/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Regression: LatteComponents.ComboBox popup rows rendered textless and
// collapsed to their padding for JS-array models. The Qt5-era delegate
// branched on Array.isArray(control.model), but Qt6's ComboBox hands the
// model back as a QVariantList even when a JS array was assigned, so the
// role lookups always fell through to model[role] - undefined for array
// models. The three model kinds below are the ones the config pages
// actually feed this component (object arrays, ListModels, string arrays).
import QtQuick 2.7
import QtTest 1.2

import org.kde.latte.components 1.0 as LatteComponents

Item {
    id: root
    width: 600
    height: 400

    LatteComponents.ComboBox {
        id: arrayCombo
        width: 300
        model: [
            { name: "Plasma Theme Colors", value: 0 },
            { name: "Dark Colors", value: 3 },
            { name: "Light Colors", value: 4 }
        ]
        textRole: "name"
        currentIndex: 0
    }

    LatteComponents.ComboBox {
        id: listModelCombo
        y: 60
        width: 300
        model: ListModel {
            ListElement { name: "On Primary Screen"; icon: "favorite" }
            ListElement { name: "On All Screens"; icon: "favorite" }
        }
        textRole: "name"
        iconRole: "icon"
        currentIndex: 0
    }

    LatteComponents.ComboBox {
        id: stringCombo
        y: 120
        width: 300
        model: ["Disabled scrolling", "Enabled scrolling"]
        currentIndex: 0
    }

    TestCase {
        name: "ComboBoxPopup"
        when: windowShown

        function popupTexts(combo) {
            combo.popup.open();
            tryCompare(combo.popup, "opened", true);
            wait(50);
            var lv = combo.popup.contentItem;
            var out = [];
            for (var i = 0; i < lv.count; ++i) {
                lv.currentIndex = i;
                out.push({ text: lv.currentItem.text, height: lv.currentItem.height });
            }
            combo.popup.close();
            tryCompare(combo.popup, "visible", false);
            return out;
        }

        function test_array_model_rows_have_text_and_height() {
            var rows = popupTexts(arrayCombo);
            compare(rows.length, 3);
            compare(rows[0].text, "Plasma Theme Colors");
            compare(rows[1].text, "Dark Colors");
            compare(rows[2].text, "Light Colors");
            for (var i = 0; i < rows.length; ++i) {
                // a padding-only row is ~8px; a real row carries label height
                verify(rows[i].height > 12, "row " + i + " collapsed: " + rows[i].height);
            }
        }

        function test_listmodel_rows_have_text() {
            var rows = popupTexts(listModelCombo);
            compare(rows.length, 2);
            compare(rows[0].text, "On Primary Screen");
            compare(rows[1].text, "On All Screens");
        }

        function test_string_model_rows_have_text() {
            var rows = popupTexts(stringCombo);
            compare(rows.length, 2);
            compare(rows[0].text, "Disabled scrolling");
            compare(rows[1].text, "Enabled scrolling");
        }
    }
}
