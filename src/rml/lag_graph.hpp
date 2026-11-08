#pragma once
namespace cod4x::rml {
// <lag-graph frames="..." snaps="..." delayed="...">: the stock lagometer's 48 newest samples, newest at the right.
// frames: frame time against the newest snapshot in ms (negative: interpolating, positive: extrapolating);
// snaps: snapshot ping in ms (-1: dropped, 0: none); delayed: one 0/1 per snapshot (rate-delayed).
// Drawn as two bands like the stock graph (CoD4 1.7 CG_DrawLagometer): frames around a midline in the upper half,
// scaled 16 units per 300 ms each way; snapshots rising from the bottom, 24 units per 900 ms.
void RegisterLagGraph();
}
