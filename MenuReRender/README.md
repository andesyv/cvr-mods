# MenuReRender
This is a BepInEx mod that renders the menu and the quickmenu on top of the screen after everything else, making it virtually impossible (at least to my knowledge) for people to block your access to the menu. This mod only works on desktop.

## Why desktop only?
The mod makes use of Unity's [Command Buffers](https://docs.unity3d.com/2019.4/Documentation/Manual/GraphicsCommandBuffers.html) which enable you to add custom rendering events to cameras that can be run at various steps within the rendering pipeline. Unfortunately, doing this in VR created weird artifacts where the image would either be stretched over both eyes or only show up in one eye. [Apparently this seems like a bug with the Unity version itself](https://issuetracker.unity3d.com/issues/vr-command-buffers-placed-at-afterdepthtexture-get-stretches-over-viewport-when-forward-rendering-plus-single-pass-stereo-used), but it seems to work in later versions of Unity and with games using a scriptable render pipeline. I've therefore decided to wait for ChilloutVR to update to a newer version of Unity before adding VR support.