# Local ImGui extensions

This directory retains the upstream ImGui version, license and commit recorded in
`cgmanifest.json`. It is not an upstream version upgrade.

`ImGuiIO::DiscardMouseInput()` is an engine-fork cancellation API. Call it between
frames, after ending any prior frame and before `NewFrame()`. It operates on that
IO's context, without changing the current binding. It discards queued mouse,
touch and pen position/button/wheel events, resets mouse state and click history,
and cancels deferred wheel input and mouse-owned drag/drop. It does not synthesize
a release or discard keyboard, modifier, gamepad, text or focus events. Text
editing and navigation retain their state; other widgets retire their canceled
mouse interaction through their normal lifecycle. This is not a lasting input
policy or an OS cursor operation.

The engine's `ezImgui::SetCurrentContextForView(view, passMouseInput)` selects
mouse delivery on the first bind of each view/frame. Later binds only bind the
context. Existing one-argument calls enable mouse input; the existing all-input
switch still takes precedence. Consumers must clear their thread-local binding
at the end of every UI batch and must not mutate persistent `NoMouse` flags to
control a later frame.

On vendor updates, reconcile this operation with upstream input bookkeeping,
wheel deferral and drag/drop lifecycles. Run the `ImGuiInput`, `ImGui Input Backend`
and existing `ImGui Lifetime` GameEngineTest coverage; real shared-console/widget
acceptance remains necessary for consuming applications.
