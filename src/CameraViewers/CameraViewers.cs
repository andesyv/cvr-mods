using BepInEx;

namespace CameraViewers
{
    [BepInPlugin(MyPluginInfo.PLUGIN_GUID, MyPluginInfo.PLUGIN_NAME, MyPluginInfo.PLUGIN_VERSION)]
    [BepInProcess("ChilloutVR.exe")]
    public class CameraViewers : BaseUnityPlugin
    {
        private void Awake()
        {
            Logger.LogDebug("Hello CameraViewers!");
        }
    }
}