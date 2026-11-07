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

## Standard Promod variant (2026-11-07)

The same economy document/controller also renders standard loadouts. The server publishes `flat_loadout_mode=standard` and `flat_loadout_ready=1`; the native adapter adopts that transport as well as live economy menus. Older economy servers retain their existing live flag behavior. `economy_mode` controls visibility; standard keeps cards/pickers/camo/tactical/team rows and hides prices, wallet, armor, gifts, rebuy and buy phase. Standard paid shortcuts are inactive and server paid/lab responses are rejected. Its primary action is CONFIRM, using the existing deploy/class acceptance route. `ui_flat_loadout=0` selects stock server menu routing; the existing `r_rmlEconomy` client setting controls adoption for both modes.

Regular class limits and server equipment eligibility remain authoritative. Standard mode never applies the lab's forced slot limits or Demolition purchase confirmation. No second live controller or copied document was introduced. Changing mode clears stale hover help and pending UI state. The spectator HUD inventory is separate; a passive HUD path has not been implemented by this menu change.

2026-11-07 menu feedback fixes: stock `ui_mp/cac_ingame.inc::LOCAL_PRIMARY_WEAPON_ACTION` locally sets the displayed weapon before its server response; the RML native bridge now does the same for allowlisted primary/sidearm choices. Focus loss clears held input while retaining adopted menu suppression and presentation, rather than restoring native visuals. Standalone tests verify visible input clearing and canceled held clicks; all six CTests pass (9.17s). Hover audio ignores programmatic focus and is suppressed briefly after a click. Native human audio/focus verification remains a separate check.
