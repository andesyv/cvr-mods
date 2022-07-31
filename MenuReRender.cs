using System;
using System.Linq;
using System.Reflection;
using ABI_RC.Core.InteractionSystem;
using BepInEx;
using cohtml;
using HarmonyLib;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MenuReRender
{
    [BepInPlugin("dev.syvertsen.cvr.plugins.menurerender", "MenuReRender", "0.0.1")]
    [BepInProcess("ChilloutVR.exe")]
    public class MenuReRender : BaseUnityPlugin
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private GameObject _mainMenu;
        private RenderReplacer _replacerInstance;
        private AssetBundle _assets;
        private static Material _uiMaterial;
        private Action<bool> _onMenuToggle;
        private Harmony _harmonyInstance;
        private static MenuReRender _instance;

        private void Awake()
        {
            _instance = this;
            if (!Init())
                SceneManager.sceneLoaded += OnSceneWasLoaded;
        }

        public void OnSceneWasLoaded(Scene scene, LoadSceneMode mode)
        {
            if (Init())
                SceneManager.sceneLoaded -= OnSceneWasLoaded;
        }

        private bool Init()
        {
            _mainMenu = FindMainMenu();
            if (!_mainMenu)
                return false;

            if (!LoadAssets())
                return false;

            // Setup RenderReplacer components
            _replacerInstance = _mainMenu.AddComponent<RenderReplacer>();
            if (!_replacerInstance)
                return false;

            _replacerInstance.animator = ViewManager.Instance.uiMenuAnimator;
            _replacerInstance.material = _uiMaterial;
            _onMenuToggle += (isEnabled) => _replacerInstance.enabled = isEnabled;

            // Patch functions
            try
            {
                _harmonyInstance =
                    Harmony.CreateAndPatchAll(typeof(MenuReRender), "dev.syvertsen.cvr.plugins.menurerender");
            }
            catch (Exception e)
            {
                Logger.LogError(e);
                return false;
            }

            Logger.LogInfo("MenuOverRenderer done initializing!");
            return true;
        }

        private void OnDestroy()
        {
            Logger.LogDebug("Running cleanup!");

            _harmonyInstance?.UnpatchSelf();

            if (_replacerInstance)
                Destroy(_replacerInstance);

            if (_uiMaterial)
                Destroy(_uiMaterial);
            if (_assets)
                _assets.Unload(true);
        }

        private static GameObject FindMainMenu()
        {
            var objects = FindObjectsOfType<CohtmlView>();
            return objects.FirstOrDefault(view =>
                    string.Equals(view.gameObject.name, MainMenuViewName, StringComparison.OrdinalIgnoreCase))
                ?.gameObject;
        }

        private bool LoadAssets()
        {
            Logger.LogDebug("Loading assets");
            var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("MenuReRender.Resources.Assets.Bundle.menurerender.bundle");
            if (stream == null)
            {
                Logger.LogError("Failed to fetch reasource stream from assembly!");
                return false;
            }

            _assets = AssetBundle.LoadFromStream(stream);
            if (!_assets)
            {
                Logger.LogError("Failed to load assetbundle!");
                return false;
            }

            _assets.hideFlags |= HideFlags.DontUnloadUnusedAsset;

            _uiMaterial = _assets.LoadAsset<Material>("ui.mat");
            if (!_uiMaterial)
            {
                Logger.LogError("Failed to fetch material from assetbundle!");
                return false;
            }

            _uiMaterial.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            _uiMaterial.EnableKeyword("UNITY_UV_STARTS_AT_TOP");

            Logger.LogDebug("Finished loading assets");

            return true;
        }

        [HarmonyPatch(typeof(ViewManager), nameof(ViewManager.UiStateToggle), typeof(bool))]
        [HarmonyPostfix]
        private static void UiTogglePatch(ViewManager __instance, bool show)
        {
            _instance._onMenuToggle?.Invoke(show);
        }
    }
}