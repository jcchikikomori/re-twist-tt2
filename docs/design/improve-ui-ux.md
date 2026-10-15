# Text Twist 2 — Improvising the User Interface & User Experience

> This design document consists of improvements that aren't available on the original game

## Upscaled Graphics

- Currently, the game would run in the maximum native resolution which is `1024x768` (i presume), and there is no implementation to upscale the textures, fonts, etc.
- **To the LLM:** If additional tech stack required, please inform the developer

### Graphics pipeline

- The original game uses DirectX 7/8 (based on the system requirements)
- I want this game to use Vulkan graphics pipeline
- OpenGL is also supported, as the fallback when Vulkan is unavailable
- DirectX is currently being ruled out (for now) because it uses the proprietary graphics technology made by Microsoft Corporation.
- Default graphics pipeline will be the software-based if neither Vulkan nor OpenGL is available, and it will disable graphic enhancements, to ensure performance is solid

## Quality-of-life Improvements

### Separate dialog

- On the main menu, replace the "GameHouse - More Great Games" element with our own button for Quality-of-life improvements
- We are going to have a separate dialog for managing improvements
- On the Existing Settings dialog, we should disable/hide the Fullscreen option, since it will be migrated into our own

### Change Resolution

- In Settings, we should be able to change the resolution, **up to 1080p** (for the meantime).
- The fullscreen modes should be either Borderless or Native Fullscreen.

### Widescreen

- The original game only supports 4:3 screen ratio.
- Use procedural generation, and colour pallete from the original game to fill up the blank/black pixels for the widescreen (initial support should be 16:9 screen ratio).

### Screen Refresh Rate

- The original game only supports 60hz.
- This game should support high refresh rate.
