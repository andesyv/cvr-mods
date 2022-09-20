# CameraViewers
The plan for this mod is to let you spawn multiple cameras. Multiple cameras in themselves aren't that useful tho, so the meat of the mod is to spawn external windows displaying the contents of said cameras which would allow you to do stuff like staging scenes, multiple simultaneous camera angles for movies / streaming and more...

## The Plan
... is not so simple...

The naive and initial plan was to create external programs which would read from a input source like a system pipe and would then display the results. However, that would require piping large amounts of data between processes and, the likely slower part, download large image resources from the GPU. Instead, the much more complicated but likely faster plan is to directly share the image resources with the display programs themselves. This plan is further complicated by the fact that this functionality is only really well supported in Vulkan (I could at least not find any resources on it in openGL), and ChilloutVR likely does not use Vulkan as their rendering backend, meaning Vulkan modules won't be included. So.. the plan includes:
 - A ChilloutVR mod written in C#
   - The mod would do the ChilloutVR specific things like spawning cameras and fetching RenderTextures
 - A native c++ Unity plugin using OpenGL (hopefully bundled together with the mod)
   - The native plugin would be responsible for connecting textures with the viewer programs either by blitting into client memory or by using memory directly
   - Could also be responsible for managing the viewer programs (C# code can also do that)
 - A viewer program written in Rust using Vulkan.
   - Would simply wait for images and draw whenever possible
   - Would also wait for incoming commands like window resizes or program termination


