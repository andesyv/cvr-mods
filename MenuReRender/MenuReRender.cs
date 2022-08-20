using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using ABI_RC.Core.InteractionSystem;
using ABI_RC.Core.Savior;
using BepInEx;
using cohtml;
using HarmonyLib;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MenuReRender
{
    [BepInPlugin("dev.syvertsen.cvr.plugins.menurerender", "MenuReRender", "1.0.0")]
    [BepInProcess("ChilloutVR.exe")]
    public class MenuReRender : BaseUnityPlugin
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string QuickMenuName = "QuickMenu";

        private readonly List<RenderReplacer> _replacerInstances = new();
        private AssetBundle _assets;
        private Material _uiMaterial;
        private Action<bool> _onMenuToggle, _onQuickMenuToggle;
#if DEBUG
        private Harmony _harmonyInstance;
#endif

        private static MenuReRender _instance;

        private void Awake()
        {
            if (MetaPort.Instance.isUsingVr)
            {
                Logger.LogInfo("MenuReRender is disabled due to VR being enabled");
                return;
            }

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
            var mainMenu = FindMenu(MainMenuViewName);
            if (!mainMenu)
                return false;

            var quickMenu = FindMenu(QuickMenuName);
            if (!quickMenu)
                return false;

            if (!LoadAssets())
                return false;

            // Setup RenderReplacer components
            _replacerInstances.Add(CreateReplacer(mainMenu, ViewManager.Instance.uiMenuAnimator, ref _onMenuToggle));
            _replacerInstances.Add(CreateReplacer(quickMenu, CVR_MenuManager.Instance.quickMenuAnimator,
                ref _onQuickMenuToggle));
            if (_replacerInstances.Any(replacer => !replacer))
                return false;

            // Patch functions
            try
            {
#if DEBUG
                _harmonyInstance =
#endif
                Harmony.CreateAndPatchAll(typeof(MenuReRender), "dev.syvertsen.cvr.plugins.menurerender");
            }
            catch (Exception e)
            {
                Logger.LogError(e);
                return false;
            }

            Logger.LogInfo("MenuReRender done initializing!");
            return true;
        }

        private RenderReplacer CreateReplacer(GameObject menu, Animator animator, ref Action<bool> onMenuToggle)
        {
            var replacer = menu.AddComponent<RenderReplacer>();
            if (!replacer)
                return replacer;

            replacer.animator = animator;
            replacer.material = _uiMaterial;
            onMenuToggle += (isEnabled) => replacer.enabled = isEnabled;
            return replacer;
        }

#if DEBUG
        private void OnDestroy()
        {
            Logger.LogDebug("Running cleanup!");

            _harmonyInstance?.UnpatchSelf();

            foreach (var replacer in _replacerInstances.Where(replacer => replacer))
                Destroy(replacer);

            if (_uiMaterial)
                Destroy(_uiMaterial);
            if (_assets)
                _assets.Unload(true);
        }
#endif

        private static GameObject FindMenu(string name)
        {
            var objects = FindObjectsOfType<CohtmlView>();
            return objects.FirstOrDefault(view =>
                    string.Equals(view.gameObject.name, name, StringComparison.OrdinalIgnoreCase))
                ?.gameObject;
        }

        private bool LoadAssets()
        {
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
            // _uiMaterial.EnableKeyword("UNITY_UV_STARTS_AT_TOP");

            return true;
        }

        [HarmonyPatch(typeof(ViewManager), nameof(ViewManager.UiStateToggle), typeof(bool))]
        [HarmonyPostfix]
        private static void UiTogglePatch(bool show)
        {
            _instance._onMenuToggle?.Invoke(show);
        }

        [HarmonyPatch(typeof(CVR_MenuManager), nameof(CVR_MenuManager.ToggleQuickMenu), typeof(bool))]
        [HarmonyPostfix]
        private static void ToggleQuickMenuPatch(bool show)
        {
            _instance._onQuickMenuToggle?.Invoke(show);
        }
    }
}