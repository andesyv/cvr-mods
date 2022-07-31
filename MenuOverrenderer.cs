using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Resources;
using ABI_RC.Core.InteractionSystem;
using BepInEx;
using BepInEx.Configuration;
using BepInEx.Logging;
using cohtml;
using HarmonyLib;
using JetBrains.Annotations;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;
using UnityEngine.Serialization;
using Logger = BepInEx.Logging.Logger;

namespace MenuOverrenderer
{
    [BepInPlugin("dev.syvertsen.plugins.menuoverrenderer", "MenuOverrenderer", "0.0.1")]
    [BepInProcess("ChilloutVR.exe")]
    public class MenuOverrenderer : BaseUnityPlugin
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string PlayerLocalTransformName = "_PLAYERLOCAL";
        private static readonly int RenderDisabledLayer = LayerMask.NameToLayer("Ignore Raycast");
        private GameObject _mainMenu;
        private RenderReplacer _replacerInstance = null;
        private AssetBundle _assets;
        public static Material UIMaterial;
        private Action<bool> _onMenuToggle;
        private Harmony _harmonyInstance = null;
        private IEnumerator _onMenuToggleCoroutine;
        private static MenuOverrenderer _instance;
        private bool _coroutineRunning = false;
        
        private void Awake()
        {
            _instance = this;
            if (Init())
                SceneManager.sceneLoaded += OnSceneWasLoaded;
        }

        private bool Init()
        {
            _mainMenu = FindMainMenu();
            if (!_mainMenu)
                return false;

            if (!LoadAssets())
                return false;

            _replacerInstance = _mainMenu.AddComponent<RenderReplacer>();
            if (!_replacerInstance)
                return false;

            _replacerInstance.Animator = ViewManager.Instance.uiMenuAnimator;
            
            _onMenuToggle += (isEnabled) =>
            {
                if (!isEnabled && _coroutineRunning)
                    StopCoroutine(_onMenuToggleCoroutine);
                _replacerInstance.enabled = isEnabled;
            };

                if (Camera.main)
            {
                // Camera.main.cullingMask = -0b00000000000000000001000000000011;
                // _prevCullingMask = Camera.main.cullingMask;
                // // Disable the "Ignore Raycast" built-in layer.
                // Camera.main.cullingMask = ~(1 << Rend    erDisabledLayer) & Camera.main.cullingMask;
                // // _overrenderCamera = Camera.main.gameObject.AddComponent<Camera>();
                //
                // // While rendering, move the menus to the disabled layer
                // Camera.onPreCull += CameraPreRender;
                // Camera.onPostRender += CameraPreRender;
            }

            // if (!_overrenderCamera)
            //     return false;
            

            try
            {
                _harmonyInstance =
                    Harmony.CreateAndPatchAll(typeof(MenuOverrenderer), "dev.syvertsen.cvr.plugins.menuoverrider");
            }
            catch (System.Exception e)
            {
                Logger.LogError(e);
                return false;
            }

            Logger.LogInfo("MenuOverrenderer done initializing!");
            return true;
        }

        public void OnSceneWasLoaded(Scene scene, LoadSceneMode mode)
        {
            if (Init())
                SceneManager.sceneLoaded -= OnSceneWasLoaded;
        }

        private void OnDestroy()
        {
            Logger.LogInfo("Goodbye world!");

            _harmonyInstance?.UnpatchSelf();

            if (_replacerInstance)
                Destroy(_replacerInstance);

            // if (Camera.main)
            //     Camera.main.cullingMask = _prevCullingMask;
            
            if (UIMaterial)
                Destroy(UIMaterial);
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

        // private void CameraPreRender(Camera _)
        // {
        //     _prevMenuLayer = _mainMenu.layer;
        //     _mainMenu.layer = LayerMask.NameToLayer("Ignore Raycast");
        // }
        //
        // private void CameraPostRender(Camera _)
        // {
        //     _mainMenu.layer = _prevMenuLayer;
        // }

        private bool LoadAssets()
        {
            Logger.LogDebug("Loading assets");
            // Very cool method of embedding assets into dll. Code taken from
            // https://github.com/knah/VRCMods/blob/master/UIExpansionKit/UiExpansionKitMod.cs#L105
            // and
            // https://github.com/sinai-dev/UnityExplorer/blob/master/src/UI/UIManager.cs#L446
            var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("MenuOverrenderer.Resources.Assets.Bundle.menuoverrenderer.bundle");
            if (stream == null)
            {
                Logger.LogError("Failed to fetch reasource stream from assembly!");
                return false;
            }

            // var memStream = new MemoryStream((int)stream.Length);
            // stream.CopyTo(memStream);
            // _assets = AssetBundle.LoadFromMemory(memStream.ToArray(), 0);
            _assets = AssetBundle.LoadFromStream(stream);

            if (!_assets)
            {
                Logger.LogError("Failed to load assetbundle!");
                return false;
            }

            _assets.hideFlags |= HideFlags.DontUnloadUnusedAsset;

            UIMaterial = _assets.LoadAsset<Material>("ui.mat");
            if (!UIMaterial)
            {
                Logger.LogError("Failed to fetch material from assetbundle!");
                return false;
            }

            UIMaterial.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            UIMaterial.EnableKeyword("UNITY_UV_STARTS_AT_TOP");
            
            Logger.LogDebug("Finished loading assets");

            return true;
        }
        
        [HarmonyPatch(typeof(ViewManager), nameof(ViewManager.UiStateToggle), typeof(bool))]
        [HarmonyPostfix]
        private static void UiTogglePatch(ViewManager __instance, bool show)
        {
            // if (show)
            // {
            //     // var animatorState = Traverse.Create(__instance).Field("uiMenuAnimator").GetValue<Animator>()
            //     //     .GetCurrentAnimatorStateInfo(0);
            //     // var percentageLeft = 1.0f - (animatorState.normalizedTime - (long)animatorState.normalizedTime);
            //     // var enableDelay = Math.Abs(animatorState.speedMultiplier) * animatorState.length;
            //     // // OnMenuToggle?.Invoke(show);
            //     _instance.StartOrRestartCoroutine();
            // }
            // else
            // {
            //     
            //     _instance.Logger.LogDebug($"Executing of onmenutoggle(false) now");
            // }
            _instance._onMenuToggle?.Invoke(show);
        }

        private static IEnumerator WaitSetEnabled(bool enable, float delay)
        {
            _instance.Logger.LogDebug("Coroutine start!");
            _instance._coroutineRunning = true;
            yield return new WaitForSeconds(delay);
            _instance._onMenuToggle?.Invoke(enable);
            _instance._coroutineRunning = false;
        }

        private void StartOrRestartCoroutine()
        {
            if (_coroutineRunning)
                StopCoroutine(_onMenuToggleCoroutine);
            _onMenuToggleCoroutine = WaitSetEnabled(true, 0.4f);
            StartCoroutine(_onMenuToggleCoroutine);
        }
    }

    class RenderReplacer : MonoBehaviour
    {
        private Renderer[] _renderers;
        private bool[] _enabledStates;
        private Dictionary<Camera, CommandBuffer> _cameras = new Dictionary<Camera, CommandBuffer>();
        private Material _material;
        private RenderTexture _menuRenderTexture;
        private ManualLogSource Logger { get; } = BepInEx.Logging.Logger.CreateLogSource("RenderReplacer");
        public Animator Animator;

        private void Awake()
        {
            _material = MenuOverrenderer.UIMaterial;
            _renderers = GetComponentsInChildren<Renderer>();
            _enabledStates = _renderers.Select(r => r.enabled).ToArray();
            var view = GetComponent<CohtmlView>();
            if (!view)
                Logger.LogError("View was not found!");
            else
                _menuRenderTexture = view.ViewTexture;
        }

        private bool IsVisible()
        {
            if (!Animator)
                return true;
            var clips = Animator.GetCurrentAnimatorClipInfo(0);
            return 0 < clips.Count(c => c.clip.name is "Open");
        }

        // Big inspiration taken from https://forum.unity.com/threads/rendering-an-object-multiple-times-with-different-materials.505138/
        private void OnWillRenderObject()
        {
            var cam = Camera.current;
            if (_cameras.ContainsKey(cam))
            {
                foreach (var rend in _renderers)
                    rend.enabled = false;
                
                return;
            } else if (!IsVisible())
                return;

            Logger.LogDebug($"Current buffer count: {cam.commandBufferCount}");

            // Build command buffer
            var cb = new CommandBuffer();
            var shaderID = Shader.PropertyToID("_MenuOverlay");
            // cb.GetTemporaryRT(shaderID, -1, -1, 16, FilterMode.Bilinear);
            // cb.SetRenderTarget(shaderID);
            // cb.ClearRenderTarget(true, true, Color.cyan, 0.0f);
            cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);

            foreach (var rend in _renderers)
            {
                if (_menuRenderTexture)
                {
                    cb.SetGlobalTexture("_CurrentUITexture", _menuRenderTexture);
                    Logger.LogDebug($"_CurrentUITexture was set to {_menuRenderTexture.name}");
                }
                cb.DrawRenderer(rend, _material);
                rend.enabled = false;
            }

            // cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);
            // cb.Blit(shaderID, BuiltinRenderTextureType.CameraTarget);
            // cb.ReleaseTemporaryRT(shaderID);

            // Add command buffer
            cam.AddCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras[cam] = cb;

            Logger.LogDebug("Added command buffer!");
            Logger.LogDebug($"Current buffer count: {cam.commandBufferCount}");
        }

        private void OnDisable()
        {
            Logger.LogDebug("Removing command buffers!");
            foreach (var (cam, cb) in _cameras)
                cam.RemoveCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras.Clear();

            _renderers.Zip(_enabledStates, (rend, enable) => rend.enabled = enable);
        }

        // private void OnDestroy()
        // {
        //     foreach (var (cam, cb) in _cameras)
        //         cam.RemoveCommandBuffer(CameraEvent.AfterEverything, cb);
        // }
    }
}