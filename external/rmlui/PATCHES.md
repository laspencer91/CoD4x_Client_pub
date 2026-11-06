# RmlUiCore vendor patches

`Plugins/ProlineRml/Source/RmlUiCore/RmlUi` is vendored RmlUi 6.3 and is not edited casually (see
`docs/implementation/hud/RMLUI_CAPABILITIES.md`). When a local change to the vendored source is
unavoidable, it is kept as a patch file here in addition to being applied to the tree, so an RmlUi
upgrade can re-apply the same behavior deliberately instead of silently losing it.

To re-apply a patch after replacing the vendored `RmlUi/` tree with a new upstream drop, run from
the repo root:

```
git apply Plugins/ProlineRml/Source/RmlUiCore/patches/<file>
```

Check whether the patch still applies cleanly (`git apply --check ...` first) and re-verify the
behavior described below; upstream may have changed the surrounding code.

## 0001-animation-delay-shows-first-keyframe.patch

**What it changes:** `ElementAnimation::UpdateAndGetProperty` (`ElementAnimation.cpp`) and a new
`delay_fill_applied` member (`ElementAnimation.h`). While an animation started from the `animation`
CSS property (`ElementAnimationOrigin::Animation`) is still waiting out its `delay` (world time has
not reached the animation's start time), the property is now applied once with the value of key 0 —
the resolved `0%` keyframe if the `@keyframes` block defines one, otherwise the element's value at
the moment the animation was started (see `Element::HandleAnimationProperty`,
`Element::StartAnimation`). After that one application the method goes back to returning an empty
`Property` (as before) until the delay elapses, so the element is not re-styled every frame while
waiting (`SetProperty` dirties layout).

**Why:** RmlUi core has no `animation-fill-mode`; a delayed `animation` used to show the element's
base rule (its landed end state) for the whole delay, so a staggered entrance flashed its finished
look before playing. Every stagger in our stylesheets was written as a hold segment inside
`@keyframes` to work around this (see the letter stagger in `Content/Scrap/UI/Rml/Shared/banner.rcss`
before this patch), which is the biggest source of generated RCSS keyframe bloat. This patch makes a
delayed `animation` behave like CSS `animation-fill-mode: backwards`: it previews its starting
keyframe during the delay instead.

**Restricted to:** `ElementAnimationOrigin::Animation` only. Transitions
(`ElementAnimationOrigin::Transition`) and script/host-initiated animations
(`Element::Animate`/`ElementAnimationOrigin::User`, used by `UProlineRmlHostWidget::AnimateElement`)
keep the original silent-wait behavior — unchanged, not in scope for this patch.

**Files:**
- `Plugins/ProlineRml/Source/RmlUiCore/RmlUi/Source/Core/ElementAnimation.cpp`
- `Plugins/ProlineRml/Source/RmlUiCore/RmlUi/Source/Core/ElementAnimation.h`

**Test:** `Proline.UI.Rml.AnimationDelay`
(`Plugins/ProlineRml/Source/ProlineRml/Private/ProlineRmlAnimationTests.cpp`).

**Status (2026-09-15):** built (`-Module=RmlUiCore -Module=ProlineRml`) and `Proline.UI.Rml.AnimationDelay`
passes. `Content/Scrap/UI/Rml/Shared/banner.rcss` was the first stylesheet migrated from hold
segments to delays; the palette (g12, g13, g21, g25) followed on 2026-09-16.
