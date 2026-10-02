# 01.18 control fix

The Vita input adapter correctly detected D-pad left, but the controller
initializer received GLFW's physical button count of 15. GTA translates left
to logical button 16, so the default `GO_LEFT` binding was never installed.
The other three directions received their bindings.

On Vita, the controller initializer now uses all 16 GTA button IDs for a
connected pad. The physical GLFW array retains its correct length. Before
skipping previously initialized buttons, the initializer restores `GO_LEFT`
only when its joystick binding is empty. This covers old settings that recorded
15 or 16 initialized buttons. Existing custom left bindings and other settings
are preserved; the normal startup settings writer persists the repair.

Run the regression check with:

```sh
python tools/vita/check-vita-controls.py
```

The native host test executes the production Vita button reader, button-ID
translation, default initializer, state mapper and menu handlers with controlled
hardware reads. It covers:

- Fresh settings with 15 reported GLFW buttons and 16 logical GTA buttons.
- Old settings with 15 or 16 initialized buttons and a missing left binding.
- Custom left bindings, unchanged unrelated settings and repeated initialization.
- Left/right gameplay input, simultaneous opposite directions and release.
- All four directions reaching the engine and the menu press/release handlers.
- Trigger button IDs and zero-button/disconnected initialization.
- Unchanged desktop initialization behavior.

These checks passed with native MSVC AddressSanitizer. They verify translation
and settings-table repair; they do not exercise physical Vita buttons or a live
gameplay session. After installing the full VPK over the previous version, check
D-pad left during gameplay without deleting existing settings or save files.

The complete game and intro launcher built with the native VitaSDK. Package
contents, ZIP CRCs, version/title ID, extended memory attribute and ARM ELF checks
passed. The full intro movie and audio are the same as in 01.17.
