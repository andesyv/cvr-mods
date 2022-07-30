using System;
using System.Linq;
using BepInEx;
using BepInEx.Configuration;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MenuOverrenderer
{
    [BepInPlugin("dev.syvertsen.plugins.menuoverrenderer", "MenuOverrenderer", "0.0.1")]
    [BepInProcess("ChilloutVR.exe")]
    public class MenuOverrenderer : BaseUnityPlugin
    {
        private void Awake()
        {
            Logger.LogInfo("Hello world!");
        }

        private void OnDestroy()
        {
            Logger.LogInfo("Goodbye world!");
        }
    }
}