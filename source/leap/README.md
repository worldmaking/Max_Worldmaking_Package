# Leap for Max/MSP/Jitter

A native (Mac + Windows) Max/MSP object for interfacing with LeapMotion controller.

Aiming to expose as much of the v2 SDK capabilities as possible.

New project, many things may still change (including object name!)

Working:
- Connection status, FPS
- IR images
- IR warp calibration images
- Hands (palm, arm)
- Fingers (bones)
- Tools
- Grip / pinch
- Confidence
- Nearest hand ID
- @unique, @allframes and @background to control when frames are processed
- @hmd for the LeapVR optimization
- Gesture recognition (circle, swipe, key & screen taps)
- Frame serialization/deserialization (example via jit.matrixset)
- Backwards-compatibility option with [aka.leapmotion] via @aka 1 

Work-in-progress:
- Fix basis-to-quat conversion, seems odd
- Option to export bones as matrices (for e.g. jit.gl.multiple or jit.gl.mesh)?
- Visualizer (wip)
- IR image warp/rectification shader, e.g. see-through AR (wip)
- Motion tracking between frames (wip)
- Filter output dicts for speed (e.g. turn off capturing fingers if not needed)?
- Example hooking up to a rigged hand model

## Notes on use

- You need to [install the LeapMotion v2 driver](https://www.leapmotion.com/setup)
- Windows: You need to make sure the Leap.dll is always next to the leap.mxe.
- Images: You need to enable "allow images" in the LeapMotion service for this to work.
 


