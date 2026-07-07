#!/usr/bin/env bash
# Headless compile-check for every QML file in the shell, containment and
# indicator packages (porting plan Phase 5). Compiles each file in a real QML
# engine via Qt.createComponent, so it catches removed-type and
# removed-property errors in lazy, interaction-only components (widget
# explorer, config pages) that would otherwise need a click in a live session
# to surface. It compiles, it does not instantiate: type resolution and
# property-assignment existence are checked, runtime binding evaluation is
# not.
#
# Skipped file classes, reported in the output:
#   * files importing org.kde.latte.private.app - that module is registered
#     inside the latte-dock binary (lattecorona.cpp), it never exists for a
#     standalone engine; these all load during dock startup anyway
#   * superseded *.5.2[0-5].qml version-ladder variants - on Plasma 6 only
#     the newest variant is ever loaded (see ToolTipInstance.qml's selector);
#     the older rungs target removed Plasma 5 APIs and are dead here
#
# Runs inside the flake devShell (ctest invokes it there via build-check.sh).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

source "$repo/scripts/lib-qml-env.sh"
qml_env_setup "$repo"
qml_env_stage

mapfile -t all < <(find \
    "$stage/share/plasma/shells/org.kde.latte.shell" \
    "$stage/share/plasma/plasmoids/org.kde.latte.containment" \
    "$stage/share/plasma/plasmoids/org.kde.latte.plasmoid" \
    "$stage/share/latte/indicators" \
    -name '*.qml' 2>/dev/null | sort)

if [[ "${#all[@]}" -eq 0 ]]; then echo "no staged QML found under $stage"; exit 2; fi

files=(); skipped_app=0; skipped_ladder=0
for f in "${all[@]}"; do
    if grep -q 'org.kde.latte.private.app' "$f"; then skipped_app=$((skipped_app+1)); continue; fi
    if [[ "$f" =~ \.5\.2[0-5]\.qml$ ]]; then skipped_ladder=$((skipped_ladder+1)); continue; fi
    files+=("$f")
done
echo "skipped $skipped_app app-module-dependent + $skipped_ladder dead-version-ladder files"

gen="$stage/_compile_gate.qml"
{
    echo 'import QtQuick'
    echo 'import QtTest'
    echo 'TestCase {'
    echo '    name: "QmlCompileGate"'
    echo '    property var files: ['
    for f in "${files[@]}"; do echo "        \"file://$f\","; done
    echo '    ]'
    echo '    function test_compileAll() {'
    echo '        var failed = [];'
    echo '        for (var i = 0; i < files.length; i++) {'
    echo '            var c = Qt.createComponent(files[i]);'
    echo '            if (c.status === Component.Error) {'
    echo '                console.warn("FAIL " + files[i] + "\n      " + c.errorString().trim());'
    echo '                failed.push(files[i]);'
    echo '            }'
    echo '            if (c) c.destroy();'
    echo '        }'
    echo '        console.warn("=== " + failed.length + " of " + files.length + " package QML files failed to compile ===");'
    echo '        verify(failed.length === 0, failed.length + " QML files failed to compile");'
    echo '    }'
    echo '}'
} > "$gen"

echo "compiling ${#files[@]} QML files (offscreen)..."
QT_QPA_PLATFORM=offscreen qmltestrunner "${imports[@]}" -input "$gen"
