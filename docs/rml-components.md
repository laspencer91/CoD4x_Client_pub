# Reusable CoD4 UI controls

Updated 2026-11-07. The client owns components and shared styling; the mod owns economy layout, stock art and server rules. The Strikepoint import is preserved under `docs/design/strikepoint` in the mod repository.

Link `shared/tactical.rcss` and `shared/hardware.rcss`, then add `hardware-ui` to the screen. Reuse `.surface-card.scanlined`, `.attachment-tile.selected`, `.keycap`, `.centered-button`, `.holo-action` and `.charge-meter` / `.charge-segment.filled`. Game-specific positioning belongs in the mod stylesheet. Texture gradients and fine scanlines work with the current geometry renderer; shader filters and scene blur are not implemented.

Use charcoal/grey for surfaces, amber for focus/selection and actions, green for available cash/ownership, blue for armor charge segments, and red only for adverse state. Keep numbers and prices immediate. Use short equipment names, contained stock images and consistent image aspect ratios. The 1920x1080 economy stage scales uniformly; palette screens use 1280x800.

## Controlled toggle

```xml
<ui-toggle data-attrif-checked="enabled" data-econ="repeat_buy" />
```

`checked` and `disabled` are presence attributes; use `data-attrif-*` for booleans. The internal native button forwards the action/help/value attributes. The owner updates checked state after acceptance: clicking never assumes a successful server transaction. `on-label` and `off-label` customize ON/OFF text. Hover/focus, keyboard activation and thumb motion share one implementation. F8 sample 18 demonstrates the control with local state.

## Optional modal backdrop dismissal

```xml
<ui-modal dismiss-on-backdrop="true" dismiss-action="primary-picker">
    <div class="picker-panel" modal-panel="true">...</div>
</ui-modal>
```

The caller supplies fullscreen backdrop and panel positioning. Mark every protected panel with `modal-panel`; descendant clicks stay inside. The optional `dismiss-on-backdrop` attribute emits a bubbling `modalclose` event with reason `backdrop`. Omit it, or set false/0, to prevent backdrop dismissal. The host interprets `dismiss-action`; the component does not alter application state. Pickers enable dismissal, restore focus to their opener and also support Escape. Purchase confirmations require explicit actions.

## Preview, authority and reload

F7 opens an isolated mutable economy fixture: select weapons/classes, cycle skins/tactical, buy/refund items, gift cash, toggle and reorder rebuy. RESET restores defaults; +$250 provides cash to test the paid class. It makes no host purchase calls. `economy_preview.*` owns this simulation separately from the live dvar adapter. Live prices, ownership, affordability, refunds and cash remain server controlled; actions pass a strict allowlist and reuse GSC menu responses.

Both source roots reload automatically after saves while the game is open. Shared styles resolve from the client; mod files override matching paths. Native component changes require rebuilding/restarting the client. Packaged assets require restaging/rebuilding their archive. F9 requests reload; F8 opens the palette.

Keep native transport menu assets: they carry server open/close/confirmation semantics and provide compatibility fallback. The RML-enabled economy client displays the new menu by default (`r_rmlEconomy=1`); standard-mode and unavailable-host fallback remain supported. Do not duplicate purchase rules in a second live UI controller.

Verification: all six CTests, including D3D9 economy controls, pass. The mod records dedicated server regression results and the parity audit in `docs/evidence/2026-11-07-new-buy-menu.md`. Desktop fixture checks do not establish multiplayer acceptance or frame-time performance.
