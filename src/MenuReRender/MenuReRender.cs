using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using ABI_RC.Core.InteractionSystem;
using MelonLoader;
using HarmonyLib;
using cohtml;
using UnityEngine;
using UnityEngine.SceneManagement;
using ABI_RC.Core.Savior;

[assembly: MelonGame("Alpha Blend Interactive", "ChilloutVR")]
[assembly: MelonInfo(typeof(MenuReRender.MenuReRender), MenuReRender.MenuReRender.DisplayName, MenuReRender.MenuReRender.Version, "Andough", "http://andough.com")]
[assembly: AssemblyVersion(MenuReRender.MenuReRender.Version)]
[assembly: AssemblyFileVersion(MenuReRender.MenuReRender.Version)]
[assembly: AssemblyProduct(MenuReRender.MenuReRender.DisplayName)]
[assembly: AssemblyDescription("ChilloutVR mod that renders your menu on top of your screen")]

namespace MenuReRender
{
  public class MenuReRender : MelonMod
  {
    public const string DisplayName = "MenuReRender";
    public const string Version = "1.2.0";

    private const string MainMenuViewName = "CohtmlWorldView";
    private const string QuickMenuName = "QuickMenu";

    private readonly List<RenderReplacer> _replacerInstances = new();
    private Material _uiMaterial;
    private Action<bool> _onMenuToggle, _onQuickMenuToggle;

    private static MenuReRender _instance;

    public override void OnInitializeMelon()
    {
      // Commandbuffers still seems to not work with VR :/
      // With the update to Unity 2021, some CommandBuffer bugs might have been fixed.
      // So the it may not work in VR due simply due to something I have done incorrectly.
      // TODO: Try some different things here: https://forum.unity.com/threads/commandbuffer-blit-in-vr-xr.1204489/
      if (MetaPort.Instance.isUsingVr)
      {
        MelonLogger.Msg("MenuReRender is disabled due to VR being enabled");
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
      try
      {
        // Too early to init. Try again later
        if (!ViewManager.Instance || !ViewManager.Instance.uiMenuAnimator || !CVR_MenuManager.Instance || !CVR_MenuManager.Instance.quickMenuAnimator)
          return false;

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
        _replacerInstances.Add(CreateReplacer(quickMenu, CVR_MenuManager.Instance.quickMenuAnimator, ref _onQuickMenuToggle));

        if (_replacerInstances.Any(replacer => !replacer))
          return false;

        // Patch functions
        try
        {
          HarmonyInstance.PatchAll(typeof(MenuReRender));
        }
        catch (Exception e)
        {
          MelonLogger.Error(e);
          return false;
        }

        MelonLogger.Msg("MenuReRender done initializing!");
      }
      catch (Exception e)
      {
        MelonLogger.Error(e);
        return false;
      }

      return true;
    }

    private RenderReplacer CreateReplacer(GameObject menu, Animator animator, ref Action<bool> onMenuToggle)
    {
      var replacer = menu.AddComponent<RenderReplacer>();
      if (!replacer)
      {
        MelonLogger.Error("Failed to add RenderReplacer component to menu");
        return null;
      }

      //if (!_uiMaterial)
      //{
      //  MelonLogger.Error("UI Material somehow went out of scope :/");
      //  return null;
      //}

      replacer.animator = animator;
      replacer.material = _uiMaterial;
      onMenuToggle += (isEnabled) => replacer.enabled = isEnabled;
      return replacer;
    }

    private static GameObject FindMenu(string name)
    {
      var objects = UnityEngine.Object.FindObjectsOfType<CohtmlView>();
      return objects.FirstOrDefault(view => string.Equals(view.gameObject.name, name, StringComparison.OrdinalIgnoreCase))?.gameObject;
    }

    private bool LoadAssets()
    {
      var resourcePath = GetType().Namespace + ".menurerender.bundle";
      var stream = System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream(resourcePath);

      if (stream == null || stream.Length == 0)
      {
        MelonLogger.Error("Failed to fetch resource stream from assembly!");
        return false;
      }

      var assetBundle = AssetBundle.LoadFromStream(stream);
      if (!assetBundle)
      {
        MelonLogger.Error("Failed to load assetbundle!");
        return false;
      }

      _uiMaterial = assetBundle.LoadAsset<Material>("ui.mat");
      if (!_uiMaterial)
      {
        MelonLogger.Error("Failed to fetch material from assetbundle!");
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