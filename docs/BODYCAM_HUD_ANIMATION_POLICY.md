# Weapon lowering during HUD animations

A scripted HUD animation can scale down Bodycam's weapon lowering while it plays.
Other Bodycam layers are not affected.

## Full HUD motions

```lua
game.play_hud_motion_bodycam(
    hand,
    section,
    animation,
    mix_in,
    speed,
    lowering_influence
)
```

## HUD animation layers

```lua
game.play_hud_anm_bodycam(
    animation,
    part,
    speed,
    power,
    looped,
    no_restart,
    pivot_bone,
    lowering_influence
)
```

`lowering_influence` is clamped to `0..1`. A value of `0` removes the
lowering layer while the animation is fully blended in; `1` preserves normal
Bodycam behavior. The engine uses the animation's native blend weight, restores
lowering automatically when it ends or is interrupted, and applies the lowest
influence when explicit animation layers overlap.

The original `game.play_hud_motion` and `game.play_hud_anm` functions remain
unchanged and use an influence of `1`.

LAM2 animation actions may set:

```lua
action.bodycam_lowering_influence = 0
```

The NVG and headlamp compatibility scripts set it to turn lowering off during their animations.
Reloads, healing animations, persistent inertia overlays, and other untagged
HUD actions do not suppress Bodycam lowering.
