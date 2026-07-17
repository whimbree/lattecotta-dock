# 2026-07-16: sceneprobe follow-up scenes (the four owed by the P1 tick)

Executing the follow-up named in
docs/agent-logs/2026-07-16-sceneprobe-transplant.md ("Follow-ups owed")
and the adoption plan's P1 scene list: parabolic_zoom, the colorizer
ColorOverlay stack, the forced-monochromatic icon path, and indicator
glow. Each scene mirrors a named production call site in OUR tree (not
the fork's versions), declares probeExpect where a meaningful invariant
exists, and gets its golden blessed only after a two-run byte-identical
determinism check inside the nix environment.

Branch: worktree-agent-a101a650b6784ba6e (worktree merge will rebase
the hashes listed at the bottom).

## Font decision: text-free, recorded reasoning

The fork's parabolic_zoom renders Text with no font family pinned - a
latent cross-machine flake the adoption plan flagged. The alternative
was shipping/pinning a store-path font. Decision: text-free redesign,
for three reasons:

1. Fidelity: the production parabolic content is icons
   (Kirigami.Icon / the applet's contentItem), not text. The fork's
   Text "A" was incidental scene filler, so text adds no mirroring
   value at this site.
2. Precedent: the transplant already replaced Text with shapes in all
   six multieffect scenes and badgeeffect for exactly this reason;
   staying text-free keeps the whole scene set on one determinism
   story.
3. A pinned font file alone does not pin rendering: fontconfig config,
   hinting and antialiasing settings feed the rasterizer too, so "ship
   a font" actually means "pin the whole fontconfig surface". The
   text-free redesign deletes that dependency instead of managing it.

## Per-scene design notes

### parabolic_zoom.qml

Mirrors declarativeimports/abilities/items/basicitem/ParabolicItem.qml:
the zoom is SIZE-driven, not transform-driven - _contentItemContainer's
width follows newTempSize (iconSize * zoom) while anchored to the
screen edge, and zoom changes animate through Behavior on zoom
(NumberAnimation, Easing.OutCubic). The scene reproduces that shape:
a bottom-anchored container whose width/height bind to
iconSize * zoom, with zoom animated 1.0 -> 1.6 under the fixed-step
clock using the NumberAnimation-on-property form multieffect_degenerate
already proved deterministic. The animation duration (48ms) completes
before the last rendered frame (t=80ms), so the final readback is at
zoom = 1.6 exactly - the expectations do not depend on easing
interpolation.

probeExpect: an icon-colored pixel inside the zoomed box but OUTSIDE
the unzoomed box (fails if the zoom never applies or the animation
machinery stalls), the white center detail (fails if the container
scales around the wrong anchor), background at a corner and at a point
just outside the zoomed box (fails if the container overgrows).

### applet_colorizer.qml

Mirrors containment/package/contents/ui/applet/colorizer/Applet.qml
(the 1f835402 fix structure): Qt5Compat.GraphicalEffects.ColorOverlay
sampling the applet wrapper, with the item shadow as the ColorOverlay's
layer.effect (LatteComponents.ShadowedItem) rather than a sibling.
The scene exists to catch the "colorizing is a silent no-op" family:
dark content (like dark clock text) colorized towards a light
applyColor. Correct ColorOverlay semantics paint the applyColor FLAT
through the alpha; the MultiEffect.colorization substitution both
reference forks shipped multiplies by the source's gray level and
re-outputs dark pixels.

probeExpect: the content pixel reads the light applyColor with a
tolerance sized to reject the dark no-op output; a shadow-colored pixel
outside the content shape (asserts the layer-effect shadow actually
samples and paints); background where the wrapper is transparent.

### forced_monochromatic.qml

Mirrors the 1932db32 sites: ParabolicItem's side-painting Loader
(anchors.fill the content, ColorOverlay painting the palette textColor
through the icon alpha) with TaskIcon.qml's provider-stability rider -
the source item's layer held ON permanently while the overlay exists
(taskIconItem's layer.enabled gate), so the scene's ColorOverlay
samples a LAYERED source exactly like the production arrangement.
Same silent-no-op family as the colorizer: dark icon pixels forced to
a light textColor must come out light and flat.

probeExpect: two glyph-bar pixels of different source darkness both
read the same flat textColor (flatness across different source
luminances is precisely what colorization-vs-overlay distinguishes);
the transparent gap between bars reads background (alpha respected).

### indicator_glow.qml

Mirrors indicators/default/package/ui/main.qml firstPoint - the default
indicator's active-task dot as it actually instantiates
LatteComponents.GlowPoint: line-style active width, showGlow (the
glowApplyTo=All branch), glow3D with showBorder: glow3D, roundCorners,
location BottomEdge, contrastColor standing in for the indicator
shadowColor. GlowPoint is the QtQuick.Shapes port (RadialGradient/
LinearGradient ShapePaths replacing the Qt5 GraphicalEffects
gradients), so this scene pins that rendering path.

probeExpect: the line center reads the active basicColor (through the
glow3D shadow blend, measured), a point in the glow halo above the
line reads the measured gradient blend (fails if the Shapes gradients
stop painting - the glow silently vanishing family), background at a
far corner.

## Also fixed in passing

multieffect_colorize.qml's header claimed TaskIcon badges and
ParabolicItem's monochromizer use MultiEffect.colorization - stale
since 1932db32 moved both sites to Qt5Compat ColorOverlay (the comment
predates that fix's merge into this line of work). Reworded: the scene
pins the MultiEffect.colorization shader variant itself; the
production monochromize sites are ColorOverlay and now have their own
scene (forced_monochromatic.qml).

## Determinism check

(to be filled: two full gate runs pre-bless, byte-compare of all four
actual PNGs)

## Gate results

(to be filled: sceneprobe-gate full pass, ctest, coverage-ratchet,
qmllint-gate, build-check --fresh)

## Commits

(to be filled; the worktree merge will rebase these hashes)
