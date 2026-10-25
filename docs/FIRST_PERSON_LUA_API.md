# First-Person Transform API

The `first_person` module gives Lua the camera, HUD, weapon and sight transforms of the current
frame. The sources run from the raw aim, through the camera before Bodycam, to the final rendered
camera.

## Transform sources

| Source | Slot | Meaning |
|---|---:|---|
| `camera_aim` | ignored | Raw actor first-eye aim transform. It continues moving inside the Bodycam visual-camera deadzone and is the input used by the non-inertial vanilla `CTorch` path. |
| `camera_visual` | ignored | Final rendered camera after effectors and Bodycam. |
| `camera_base` | ignored | Camera-manager result before the Bodycam visual override. |
| `camera_delta` | ignored | Local transform from `camera_base` to `camera_visual`. |
| `viewmodel_delta` | ignored | Local additive Bodycam viewmodel transform. |
| `hud_root` | 0 or 1 | Final primary/right or offhand/left HUD root. |
| `hud_item` | 0, 1, or 2 | Final primary item, offhand item, or attached sight transform. |
| `hud_bone` | 0, 1, or 2 | Named bone on a final HUD item transform. |
| `hud_firepoint` | 0, 1, or 2 | Final HUD fire point and direction from the native `setup_firedeps` path. |
| `hud_firepoint2` | 0, 1, or 2 | Secondary HUD fire point. |
| `hud_shellpoint` | 0, 1, or 2 | HUD shell-ejection point. |
| `weapon_firepoint` | ignored | Active weapon world fire point and last fire direction. |
| `weapon_firepoint2` | ignored | Active weapon secondary world fire point. |
| `weapon_shellpoint` | ignored | Active weapon world shell point. |
| `sight` | ignored | Attached sight transform, falling back to the primary HUD item. |

Slots are `0` for the primary/right-hand item, `1` for the offhand/left-hand item,
and `2` for the attached optic. Pass `-1` and an empty bone name when a source does
not use them.

## Reading a transform

```lua
local pose = first_person.pose()
if first_person.get_pose("camera_aim", -1, "", pose) then
    printf("aim position: %s %s %s", pose.position.x, pose.position.y, pose.position.z)
end
```

A pose contains primitive vectors plus its publication `frame` and level
`generation`:

- `position`
- `right`
- `up`
- `forward`
- `frame`
- `generation`

`get_pose` returns `false` when the source is unavailable or stale, the slot is
empty, or the requested bone does not exist. It never retains a Lua game object.

## Functions

| Function | Result |
|---|---|
| `get_pose(source, slot, bone, output_pose)` | Copy a current transform into `output_pose`; returns `false` when unavailable. |

The HUD fire and shell-ejection points are calculated on the first request in a frame and
reused for later requests in the same frame.
