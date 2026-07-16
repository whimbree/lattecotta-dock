/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the preview tooltip's title/subtext derivation (EX-17) against the
//! REAL shipped ToolTipInstance.qml, instantiated here and driven through
//! its generateTitle()/generateSubText() functions. Written against the
//! pre-extraction QML bodies and kept green across the EX-17 cutover, so it
//! is the twin-equivalence proof that the C++ composer preserves shipped
//! behavior; the vectors in tests/units/tooltiptexttest.cpp are this
//! table's, cross-checked here against the shipped QML on every run.
//!
//! Context plumbing: every context-chain name ToolTipInstance resolves is
//! provided - ToolTipDelegate2/main.qml properties become properties of
//! this root, and the delegate model roles (Activities, VirtualDesktops,
//! IsOnAllVirtualDesktops, display - uppercase, so they cannot be plain QML
//! properties) arrive through a real Repeater delegate context over a
//! dynamicRoles ListModel, mirroring how the production DelegateModel
//! injects libtaskmanager's roles. dynamicRoles stores JS arrays as nested
//! ListModels, so list-valued roles are passed as array-like objects
//! ({0:...,length:N}) - shaped exactly like the reads the shipped code
//! performs on the real QVariantList roles (.length and [i]).

import QtQuick 2.7
import QtTest 1.2

import "../../plasmoid/package/contents/ui/previews" as Previews

Item {
    id: root
    width: 600
    height: 400

    // plasmoid main.qml root properties ToolTipInstance reads by id
    property bool showOnlyCurrentDesktop: false
    property bool showOnlyCurrentActivity: true

    // ToolTipDelegate2 properties reached through the context chain
    property bool isWin: true
    property bool isGroup: false
    property string appName: ""
    property var displayParent: ""
    property var genericName: undefined
    property var activitiesParent: []
    property var virtualDesktopParent: []
    property var isOnAllVirtualDesktopsParent: false
    property var windows: []
    property int pidParent: -1
    property real textWidth: 200
    property var icon: ""
    property bool hideCloseButtons: true
    property var mainToolTip: ({ launcherUrl: "", isMinimizedParent: true })
    property Item parentTask: null

    // live plasmoid singletons, mocked to the shape the functions read
    property var virtualDesktopInfo: ({
        desktopIds: ["d1", "d2", "d3"],
        desktopNames: ["One", "Two", "Three"],
        numberOfDesktops: 3
    })
    property var activityInfo: ({
        currentActivity: "curA",
        numberOfRunningActivities: 1,
        activityName: function(id) {
            var names = { curA: "Current", actB: "Work", actC: "Play", unnamed: "" };
            return names[id] !== undefined ? names[id] : "";
        }
    })
    property var mpris2Source: ({
        playerForLauncherUrl: function(url, pid) { return null; }
    })
    property var appletAbilities: ({
        myView: { screenGeometry: Qt.rect(0, 0, 1920, 1080) }
    })
    property var windowsPreviewDlg: ({ visible: false, containsMouse: false })
    property var backend: ({ cancelHighlightWindows: function() {} })
    property var tasksModel: ({ requestClose: function(idx) {} })

    ListModel {
        id: delegateModel
        dynamicRoles: true
    }

    Repeater {
        id: instanceRepeater
        model: delegateModel
        delegate: Previews.ToolTipInstance {}
    }

    TestCase {
        name: "TooltipText"
        when: windowShown

        //! ListModel/dynamicRoles stores JS arrays as nested ListModels
        //! (no .length); an array-like object survives the round trip and
        //! serves the exact reads the shipped code performs
        function arrayLike(values) {
            var wrapped = { length: values.length };
            for (var i = 0; i < values.length; ++i) {
                wrapped[i] = values[i];
            }
            return wrapped;
        }

        function setRoles(display, activities, virtualDesktops, onAllDesktops) {
            delegateModel.clear();
            delegateModel.append({
                display: display,
                Activities: arrayLike(activities),
                VirtualDesktops: arrayLike(virtualDesktops),
                IsOnAllVirtualDesktops: onAllDesktops
            });
        }

        function instance() {
            var item = instanceRepeater.itemAt(0);
            verify(item !== null, "delegate instantiated");
            return item;
        }

        function resetContext() {
            root.showOnlyCurrentDesktop = false;
            root.showOnlyCurrentActivity = true;
            root.isWin = true;
            root.isGroup = false;
            root.appName = "";
            root.displayParent = "";
            root.genericName = undefined;
            root.activitiesParent = [];
            root.virtualDesktopParent = [];
            root.isOnAllVirtualDesktopsParent = false;
            setRoles("", [], [], false);
            root.virtualDesktopInfo = {
                desktopIds: ["d1", "d2", "d3"],
                desktopNames: ["One", "Two", "Three"],
                numberOfDesktops: 3
            };
            root.activityInfo = {
                currentActivity: "curA",
                numberOfRunningActivities: 1,
                activityName: function(id) {
                    var names = { curA: "Current", actB: "Work", actC: "Play", unnamed: "" };
                    return names[id] !== undefined ? names[id] : "";
                }
            };
        }

        function checkTitle(name, expected) {
            compare(instance().generateTitle(), expected, name);
        }

        function checkSubText(name, expected) {
            compare(instance().generateSubText(), expected, name);
        }

        function test_titleStripsAppNameAndRestoresCounter() {
            resetContext();
            root.appName = "Firefox";

            root.displayParent = "index.html — Firefox";
            checkTitle("em-dash separated appName suffix stripped", "index.html");

            root.displayParent = "index.html - Firefox";
            checkTitle("hyphen separated appName suffix stripped", "index.html");

            root.displayParent = "index.html — Firefox <2>";
            checkTitle("KWin counter survives the appName strip", "index.html <2>");

            root.displayParent = "index.html <2>";
            checkTitle("counter without appName", "index.html <2>");

            root.displayParent = "index.html <2> ";
            checkTitle("counter capture includes its trailing space", "index.html <2> ");

            root.displayParent = "index.html <2>!!";
            checkTitle("counter capture includes trailing punctuation", "index.html <2>!!");

            root.displayParent = "doc - FIREFOX";
            checkTitle("appName strip is case-insensitive", "doc");

            root.displayParent = "a <b> tags — Firefox";
            checkTitle("non-numeric angle brackets are not a counter", "a <b> tags");

            root.displayParent = "a <b> <2>";
            checkTitle("counter after other angle brackets", "a <b> <2>");

            root.displayParent = "x <2> <3>";
            checkTitle("only the last counter is a counter", "x <2> <3>");

            // NBSP before the separator: JS \s is unicode-aware and strips it
            root.displayParent = "doc — Firefox";
            checkTitle("unicode whitespace stripped with the separator", "doc");

            root.displayParent = "a — b — Firefox";
            checkTitle("inner separators kept", "a — b");

            root.displayParent = "trailing dashes -- ";
            checkTitle("trailing dash run stripped without appName", "trailing dashes");
        }

        function test_titlePlaceholderWhenOnlyRedundantText() {
            resetContext();
            root.appName = "Firefox";

            root.displayParent = "Firefox";
            checkTitle("title that is only the appName becomes the em-dash", "—");

            root.displayParent = "Firefox <2>";
            checkTitle("only appName and counter leaves the counter", "<2>");

            root.displayParent = "";
            checkTitle("empty display becomes the em-dash", "—");
        }

        function test_titleAppNameIsARegexQt5Quirk() {
            resetContext();

            // Qt5 builds new RegExp(appName + "$") UNESCAPED: a metachar
            // appName is a pattern, "." matches any character
            root.appName = "Node.js";
            root.displayParent = "notes - Node.js";
            checkTitle("metachar appName still strips its literal form", "notes");

            root.displayParent = "notes - NodeXjs";
            checkTitle("metachar appName strips wildcard matches too", "notes");

            root.appName = "";
            root.displayParent = "plain title";
            checkTitle("empty appName strips nothing", "plain title");
        }

        function test_titleDeliberateDivergencesFromQt5() {
            resetContext();

            // Qt5's JS threw on new RegExp("Notepad++$") and broke the
            // whole title binding; the composer skips the appName strip
            root.appName = "Notepad++";
            root.displayParent = "doc - Notepad++";
            checkTitle("invalid appName pattern keeps the full title", "doc - Notepad++");

            // Qt5 threw on undefined.match (broken binding, empty title);
            // the shell hands the composer "" and gets the placeholder
            root.appName = "Firefox";
            root.displayParent = undefined;
            checkTitle("undefined display shows the placeholder", "—");
        }

        function test_titleNonWindowUsesGenericName() {
            resetContext();
            root.isWin = false;

            root.genericName = "Web Browser";
            checkTitle("launcher shows genericName", "Web Browser");

            root.genericName = undefined;
            checkTitle("undefined genericName shows empty", "");
        }

        function test_titleGroupChildReadsModelDisplay() {
            resetContext();
            root.appName = "Firefox";
            root.isGroup = true;

            setRoles("tab one — Firefox <3>", [], [], false);
            checkTitle("group child strips appName from model.display", "tab one <3>");
        }

        function test_subTextBlankOutQuirk() {
            // Qt5 quirk: an undefined activitiesParent suppresses the whole
            // subtext, desktops included, even for group children carrying
            // their own activities
            resetContext();
            root.activitiesParent = undefined;
            root.virtualDesktopParent = ["d2"];
            checkSubText("undefined parent activities blank the subtext", "");

            root.isGroup = true;
            setRoles("", ["actB"], ["d1"], false);
            checkSubText("blank-out applies to group children too", "");
        }

        function test_subTextDesktopLine() {
            resetContext();
            root.virtualDesktopParent = ["d2"];
            checkSubText("wayland string desktop id resolves to its name", "On Two");

            root.virtualDesktopParent = ["d1", "d3"];
            checkSubText("several desktops keep the JS default join", "On One,Three");

            root.showOnlyCurrentDesktop = true;
            checkSubText("showOnlyCurrentDesktop hides the line", "");

            resetContext();
            root.virtualDesktopParent = ["d2"];
            root.virtualDesktopInfo = {
                desktopIds: ["d1", "d2"],
                desktopNames: ["One", "Two"],
                numberOfDesktops: 1
            };
            checkSubText("a single desktop total hides the line", "");

            resetContext();
            root.virtualDesktopParent = ["d2"];
            root.isOnAllVirtualDesktopsParent = true;
            checkSubText("on-all-desktops hides the line", "");

            // X11 shape: numeric ids resolve through desktopIds the same way
            resetContext();
            root.virtualDesktopParent = [2];
            root.virtualDesktopInfo = {
                desktopIds: [1, 2, 3],
                desktopNames: ["One", "Two", "Three"],
                numberOfDesktops: 3
            };
            checkSubText("numeric X11 desktop ids resolve identically", "On Two");

            // a desktop removed between the model update and the hover:
            // its id resolves to nothing rather than an empty "On " entry
            resetContext();
            root.virtualDesktopParent = ["gone"];
            checkSubText("stale desktop id produces no line", "");
        }

        function test_subTextActivitiesLine() {
            resetContext();
            root.activitiesParent = [];
            root.activityInfo.numberOfRunningActivities = 2;
            checkSubText("empty activities on several running", "Available on all activities");

            resetContext();
            root.activitiesParent = [];
            checkSubText("empty activities on a single running is silent", "");

            resetContext();
            root.activitiesParent = ["curA", "actB"];
            checkSubText("current activity filtered, rest listed", "Also available on Work");

            resetContext();
            root.showOnlyCurrentActivity = false;
            root.activitiesParent = ["curA", "actB", "actC"];
            checkSubText("all-activities mode lists the others", "Available on Work, Play");

            resetContext();
            root.activitiesParent = ["curA"];
            checkSubText("only the current activity is silent", "");

            resetContext();
            root.activitiesParent = ["unnamed", "actB"];
            checkSubText("unnamed activities are skipped", "Also available on Work");

            resetContext();
            root.activitiesParent = ["unnamed"];
            checkSubText("only unnamed activities is silent", "");
        }

        function test_subTextCombinedLines() {
            resetContext();
            root.virtualDesktopParent = ["d2"];
            root.activitiesParent = ["actB"];
            checkSubText("desktop and activity lines join with newline",
                         "On Two\nAlso available on Work");
        }

        function test_subTextGroupChildReadsRoles() {
            resetContext();
            root.isGroup = true;
            root.showOnlyCurrentActivity = false;
            setRoles("", ["actC"], ["d3"], false);
            checkSubText("group child reads its own roles",
                         "On Three\nAvailable on Play");

            resetContext();
            root.isGroup = true;
            setRoles("", [], ["d1"], true);
            checkSubText("group child on all desktops hides the line", "");
        }
    }
}
