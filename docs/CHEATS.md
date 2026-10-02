# Cheat codes on Vita

Enter the Liberty City Stories **PSP or PS2** combinations during gameplay,
using the D-pad. Use the Vita's **L** for **L1** and **R** for **R1**. Press and
release repeated buttons separately. The rear touchpad is not required.
Gameplay control settings do not change these combinations.

| Effect | Vita buttons, in order |
| --- | --- |
| Health and vehicle repair | L, R, Cross, L, R, Square, L, R |
| Armour | L, R, Circle, L, R, Cross, L, R |
| $250,000 | L, R, Triangle, L, R, Circle, L, R |
| Weapons 1 | Up, Square, Square, Down, Left, Square, Square, Right |
| Weapons 2 | Up, Circle, Circle, Down, Left, Circle, Circle, Right |
| Weapons 3 | Up, Cross, Cross, Down, Left, Cross, Cross, Right |
| Raise wanted level | L, R, Square, L, R, Triangle, L, R |
| Clear wanted level | L, L, Triangle, R, R, Cross, Square, Circle |
| Rhino | L, L, Left, L, L, Right, Triangle, Circle |

Sequence references: [GameFAQs](https://gamefaqs.gamespot.com/psp/925776-grand-theft-auto-liberty-city-stories/cheats)
and [Grand Theft Wiki](https://www.grandtheftwiki.com/Cheats_in_GTA_Liberty_City_Stories).

The Vita build recognizes 32 LCS combinations that have existing effects in
this engine. The full mapping is in [PadCheats.inc](../src/core/PadCheats.inc).
White traffic, chrome vehicles, big heads, bike wheel size, inverted view,
calling the nearest pedestrian and multiplayer unlocks are not implemented.
The other engine platforms retain their previous cheat handling.

The tests execute the production input translation and recognizer, with
effect handlers replaced by counters. They check complete and invalid
sequences, held buttons, repeated presses, shoulder aliases, and empty
gameplay bindings. The actual gameplay effects still need a Vita test.
