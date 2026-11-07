# Text glow

Include `shared/text-effects.rcss`. Apply `text-glow` to an element for a crisp pale face and a soft blue halo. Add `text-glow-amber` for attention states. These are opt-in styles, not replacement fonts.

```xml
<div class="text-glow">ROUND WIN</div>
<decrypt-text class="text-glow" text="ROUND START" tick-ms="18" cycles="2" glitch="glitch" />
```

The effect uses RmlUi's existing `glow(outline-width blur-width offset-x offset-y color)` font effect. The presets use zero outline, a 5dp blur and no offset. RmlUi generates the halo from font glyphs; no CoD4 glow material, duplicate text nodes or custom rendering pass is required. Override the `font-effect` property for other halo colors or radii.

Keep glow for announcements and important state changes. The text face stays steady; the existing decrypt-text component owns its optional reveal/glitch behavior. A separate ghosted-copy reveal is not part of this effect. Palette section 19 demonstrates both variants and composition with decrypt-text. RML/RCSS changes use normal live reload.
