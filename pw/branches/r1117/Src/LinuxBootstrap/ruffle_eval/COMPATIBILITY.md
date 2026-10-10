# Prime World / Ruffle Compatibility Audit

This static audit predates the native adapter. See [current implementation and
runtime evidence](ADAPTER.md) for the synchronous loader, rooted handles, and C ABI
now exercised in isolation. The table's remaining-work column records the original
evaluation; it does not supersede the current port plan.

## Scope and Pin

- Official Ruffle tag: [`nightly-2026-02-13`](https://github.com/ruffle-rs/ruffle/releases/tag/nightly-2026-02-13).
- Exact source SHA: [`1b24dd3a6925eecdd1d7fa49e165814e7ed2163d`](https://github.com/ruffle-rs/ruffle/tree/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d), verified from the local official checkout at `/tmp/pw-ruffle-source`.
- Audited shipped movies: [loading `pwl.swf`][loading-swf] and [combat `main.swf`][combat-swf], including static ABC inspection (245 and 559 classes respectively). Static references do not establish runtime reachability or rendering correctness. Experimental results are maintained separately.
- Required target: **native Linux/OpenGL, without Wine or Windows client binaries**. The existing Windows path must remain unchanged. This audit establishes neither Ruffle integration nor a working OpenGL client.

## Evidence and Remaining Needs

| Area | Verified evidence / blocker | Unverified compatibility work |
| --- | --- | --- |
| `TextField.userInput` | **Confirmed loading construction blocker:** reported `Cannot create property userInput on flash.text.TextField`. Both movies write `true` in chat constructors. PW declares a native getter/setter; Ruffle's sealed TextField lacks it and rejects undeclared writes. [PW declaration][pw-userinput], [implementation][pw-userinput-set], [Ruffle class][ruffle-textfield], [write rejection][ruffle-sealed]. | A builtin compatibility implementation is needed for unchanged SWFs. Merely allowing/storing the property removes only this failure, not text parity. |
| Text formatting | PW `.text` runs through game markup. `userInput` defaults false and selects `CondenseWhite(!userInput)` / `KeepSpacesBeforePunct(userInput)`; changing it reparses and reflows text. Both movies build `<style:TT_Chat>` / `<style:TT_Players_Name>` themselves; neither references `htmlText`. [Whitespace semantics][pw-whitespace], [markup processing][pw-markup]. | **Partial `userInput` compatibility does NOT establish markup, punctuation, or reflow parity.** A host-only string translator misses strings constructed inside the SWF. Style/name-map behavior and layout need validation. |
| Loader / DDS | PW resolves movie-relative paths, strips leading `:` for game-root paths, loads PNG/DDS Bitmaps, then fires `INIT` and `COMPLETE` synchronously. Both movies' `BaseIconLoader.SetIcon` receive host-supplied paths; no DDS filename literals were found. Ruffle detects SWF/JPEG/PNG/GIF by bytes, not DDS. [PW loader][pw-loader], [Ruffle formats/sniffing][ruffle-loader]. | A native asset provider could map game paths and transcode DDS to PNG bytes while retaining the requested URL. Image fidelity, alpha, failures, and changed event timing remain untested. |
| `LoaderInfo` | No additional public API identified. Each movie references `applicationDomain` three times through standard Flash components; neither references `parameters` or loading-progress properties. PW even stubs `applicationDomain`. [PW stub][pw-loaderinfo], [Ruffle API][ruffle-loaderinfo]. | Validate lifecycle and application-domain behavior with the loaded movies; do not reproduce PW stub/null behavior as a requirement. Localization is a separate protocol below. |
| DisplayObjects / textures | C++ consumes returned DisplayObject Atoms for native blocking state, and injects an engine render texture into `miniMap_mc.miniMapAnim_mc.mapImage`. These are native host operations, not extra public DisplayObject AS members. [Blocking conversion][pw-blocking], [minimap injection][pw-minimap]. | Object-handle lifetime, blocking visuals/interaction, geometry, and live texture upload/binding need adapters. ExternalInterface cannot transport native GPU references. |
| Sound | PW `Sound.play()` reads dynamic `soundEvent`, starts an FMOD event in group `UI`, and returns null. Both movies assign 14 event names. Ruffle uses ordinary sample playback. [PW hook][pw-sound], [Ruffle playback][ruffle-sound]. | Game-audio adaptation is needed for fidelity; this is not the identified construction blocker. |
| Scale-nine bitmap | PW sets the SWF-defined `ScaleBitmap.__internalUsage__` flag to select native rendering. Both movies contain an ActionScript fallback when false. [Native optimization][pw-bitmap]. | Leave the flag false initially; validate fallback appearance/performance before adding native optimization. |

The `userInput = true` instruction offsets, relative to each movie's ABC block, are `0x1cebd` / `0x21598` in loading and `0x3e8a0` / `0x4ff38` in combat (`Chat::ChatInputTextBox` / `Chat::ChatMessageView` constructors). Among the referenced builtin classes compared, `userInput` was the only additional public member with direct bytecode references; this is not a completeness claim for runtime semantics.

## Host Contract and Wrapper Tradeoff

- **Localization:** root `LocalizationResources` returns a Class (`LoaderSources::LoaderLocalization` or combat `Localization`). Fill its static String properties, then call that Class's `LocalizationComplete()`, which dispatches `Event.COMPLETE`. It is not `LoaderInfo.parameters`, a replacement dictionary, or a call on the root. [Native sequence][pw-localization].
- **Inbound calls:** resolve the case-sensitive root getters `LoaderWindowInterface` (loading) and `mainInterface` (combat). Preserve the receiver and synchronous boolean/integer returns. Some methods return live DisplayObjects. [Native lookup/call][pw-calls], [Adventure object-return methods][pw-returns].
- **Outbound callbacks:** neither movie references ExternalInterface. Normal `FSCommands.Send(command,args)` uses `flash.system.fscommand`; the alternate test-dispatch branch defaults off. Route these through a custom [Ruffle FsCommandProvider][ruffle-fscommand]. The stock desktop handler is not the game callback bridge.

A **wrapper SWF** can register ExternalInterface `get/set/invoke` callbacks, operate on loaded content, preserve `this` using `method.apply(target,args)`, and perform localization without changing the shipped movies. Ruffle's [host call entry point][ruffle-callback] invokes registered callbacks, not arbitrary movie properties. [ExternalValue][ruffle-values] transports values/maps/lists, not VM object identity: keep DisplayObjects in a wrapper-side registry and return opaque handles, or perform their operations inside the wrapper.

A **direct native host bridge** avoids that callback/handle layer and can retain VM objects and expose renderer operations, but couples the host to pinned Ruffle internals and requires explicit rooting/lifetime management. Existing Tamarin `Atom`/`ScriptObject*` values are not transferable. Both approaches still need FSCommand routing and asset/render/audio adaptation.

**A wrapper alone cannot fix the sealed TextField constructor failure**, nor intercept all native text, sound, or texture semantics. It can replace public host plumbing after the required runtime compatibility exists; it is not proof that untouched SWFs work on stock Ruffle.

## Recommendation

Continue evaluation **conditionally**, not as a backend switch. Gate further integration on unchanged-movie construction, localization completion, synchronous typed calls/object handles, DDS/event-order behavior, and native OpenGL presentation. Evaluate text parity separately from accepting `userInput`. Keep experimental results separate, preserve Windows, and do not label the client integrated until those gates have concrete evidence.

[loading-swf]: ../../../Data/UI/Screens/Loading/Flash/pwl.swf
[combat-swf]: ../../../Data/UI/Screens/Combat/Flash/main.swf
[pw-userinput]: ../../../Vendor/Tamarin/natives/text/TextField.as#L333
[pw-userinput-set]: ../../UI/Flash/GameSWFIntegration/Natives/text/TextField.cpp#L1223
[pw-whitespace]: ../../UI/Flash/GameSWFIntegration/Natives/text/TextField.cpp#L171
[pw-markup]: ../../UI/Flash/GameSWFIntegration/TextContainer.cpp#L119
[pw-loader]: ../../UI/Flash/GameSWFIntegration/Natives/display/Loader.cpp#L39
[pw-loaderinfo]: ../../UI/Flash/GameSWFIntegration/Natives/display/LoaderInfo.h#L97
[pw-blocking]: ../../UI/FlashContainer2.cpp#L518
[pw-minimap]: ../../PF_GameLogic/Minimap.cpp#L657
[pw-sound]: ../../UI/Flash/GameSWFIntegration/Natives/media/Sound.cpp#L68
[pw-bitmap]: ../../UI/Flash/GameSWFIntegration/Natives/display/Bitmap.cpp#L56
[pw-localization]: ../../UI/Flash/GameSWFIntegration/FlashMovie.cpp#L243
[pw-calls]: ../../UI/Flash/GameSWFIntegration/FlashBaseClasses.cpp#L219
[pw-returns]: ../../PF_GameLogic/AdventureFlashInterface.cpp#L992
[ruffle-textfield]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/avm2/globals/flash/text/TextField.as#L11
[ruffle-sealed]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/avm2/object/script_object.rs#L246
[ruffle-loader]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/loader.rs#L117
[ruffle-loaderinfo]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/avm2/globals/flash/display/LoaderInfo.as#L14
[ruffle-sound]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/avm2/globals/flash/media/sound.rs#L145
[ruffle-fscommand]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/external.rs#L342
[ruffle-callback]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/player.rs#L2454
[ruffle-values]: https://github.com/ruffle-rs/ruffle/blob/1b24dd3a6925eecdd1d7fa49e165814e7ed2163d/core/src/external.rs#L23
