using System;
using System.Linq;
using System.Reflection;
using MelonLoader;
using cohtml;
using UnityEngine;
using UnityEngine.SceneManagement;
using ABI_RC.Systems.Camera;
using ABI_RC.Systems.ModNetwork;
using HarmonyLib;
using ABI_RC.Core.IO;

[assembly: MelonGame("Alpha Blend Interactive", "ChilloutVR")]
[assembly: MelonInfo(typeof(VideoCalls.VideoCalls), VideoCalls.VideoCalls.DisplayName, VideoCalls.VideoCalls.Version, "Andough", "http://andough.com")]
[assembly: AssemblyVersion(VideoCalls.VideoCalls.Version)]
[assembly: AssemblyFileVersion(VideoCalls.VideoCalls.Version)]
[assembly: AssemblyProduct(VideoCalls.VideoCalls.DisplayName)]
[assembly: AssemblyDescription("ChilloutVR mod that allows for video calls within CVR")]

namespace VideoCalls
{
  public class VideoCalls : MelonMod
  {
    public const string DisplayName = "VideoCalls";
    public const string Version = "0.1.0";
    public static readonly Guid ModNetworkId = Guid.Parse("ECB99F07-2BEF-434B-B53B-CA695D4B92B3");

    private const string MainMenuViewName = "CohtmlWorldView";
    private const string QuickMenuName = "QuickMenu";

    private const string ReadyScene = "Headquarters";

    private GameObject mDisplayPlane;
    //private VideoCallerCameraMod cameraMod;
    private string mIncomingConnection = null;
    private bool mConnectionEstablished = false;
    private VideoProjector mVideoProjector;

    public enum MessageType
    {
      ConnectionRequest,
      ConnectionHandshake,
      ConnectionData,
      DisconnectionRequest,
    }

    public struct ImageData
    {
      public ImageData(int width, int height, byte[] data, RenderTextureFormat format)
      {
        mWidth = width;
        mHeight = height;
        mData = data;
        mFormat = format;
      }
      
      public int mWidth { get; }
      public int mHeight { get; }
      public byte[] mData { get; }
      public RenderTextureFormat mFormat { get; }
    }

    public override void OnInitializeMelon()
    {
      ModNetworkManager.Subscribe(ModNetworkId, OnNetworkMessage);

      SceneManager.sceneLoaded += OnSceneWasLoaded;
    }

    //public override void OnDeinitializeMelon()
    //{
    //  ModNetworkManager.Unsubscribe(ModNetworkGuid);
    //}

    public void OnSceneWasLoaded(Scene scene, LoadSceneMode mode)
    {
      // Check if we're ready to initialize or if we're to early
      if (scene.name != ReadyScene)
        return;

      if (Init())
        MelonLogger.Msg("VideoCalls initialized successfully.");
      else
        MelonLogger.Error("Failed to initialize VideoCalls");

      SceneManager.sceneLoaded -= OnSceneWasLoaded;
    }

    private bool Init()
    {
      try
      {
        // Too early to init. Try again later
        //if (!ViewManager.Instance || !ViewManager.Instance.uiMenuAnimator || !CVR_MenuManager.Instance || !CVR_MenuManager.Instance.quickMenuAnimator)
        //  return false;

        if (!PortableCamera.Instance)
          return false;

        var mainMenu = FindMenu(MainMenuViewName);
        if (!mainMenu)
          return false;

        mDisplayPlane = GameObject.CreatePrimitive(PrimitiveType.Quad);
        if (!mDisplayPlane)
          return false;

        mDisplayPlane.transform.SetParent(mainMenu.transform, false);
        mVideoProjector = mDisplayPlane.AddComponent<VideoProjector>();

        mDisplayPlane.SetActive(false);

        var cameraMod = new CameraMod(this);
        PortableCamera.Instance.RegisterMod(cameraMod);

        //var quickMenu = FindMenu(QuickMenuName);
        //if (!quickMenu)
        //  return false;


        //PortableCamera.Instance.cameraComponent

        //if (!LoadAssets())
        //  return false;

        //// Setup RenderReplacer components
        //_replacerInstances.Add(CreateReplacer(mainMenu, ViewManager.Instance.uiMenuAnimator, ref _onMenuToggle));
        //_replacerInstances.Add(CreateReplacer(quickMenu, CVR_MenuManager.Instance.quickMenuAnimator, ref _onQuickMenuToggle));

        //if (_replacerInstances.Any(replacer => !replacer))
        //  return false;

        //// Patch functions
        //try
        //{
        //  HarmonyInstance.PatchAll(typeof(VideoCalls));
        //}
        //catch (Exception e)
        //{
        //  MelonLogger.Error(e);
        //  return false;
        //}

        MelonLogger.Msg("VideoCalls done initializing!");
      }
      catch (Exception e)
      {
        MelonLogger.Error(e);
        return false;
      }

      return true;
    }

    private static GameObject FindMenu(string name)
    {
      var objects = UnityEngine.Object.FindObjectsOfType<CohtmlView>();
      return objects.FirstOrDefault(view => string.Equals(view.gameObject.name, name, StringComparison.OrdinalIgnoreCase))?.gameObject;
    }

    private void OnNetworkMessage(ModNetworkMessage msg)
    {
      MelonLogger.Msg("Received message through the mod network!");
      msg.Read(out MessageType messageType);
      if (messageType == MessageType.ConnectionRequest)
      {
        msg.Read(out mIncomingConnection);
        MelonLogger.Msg("Connection request received");
      }
      else if (messageType == MessageType.ConnectionData)
      {
        msg.Read(out ImageData data);
        mVideoProjector.CurrentImage = data;
        MelonLogger.Msg("Image data received");
      }
      else if (messageType == MessageType.DisconnectionRequest)
      {
        mIncomingConnection = null;
        mConnectionEstablished = false;
        MelonLogger.Msg("Disconnection request received");
      }
    }

    public void Connect()
    {
      MelonLogger.Msg("Connected!");
      // More complicated connection logic to come later...
      mConnectionEstablished = true;

      mDisplayPlane.SetActive(true);
    }

    public void Disconnect()
    {
      MelonLogger.Msg("Disconnected!");
      mConnectionEstablished = false;
      mDisplayPlane.SetActive(false);
    }

    //!string.IsNullOrEmpty(networkPlayer.Uuid)

    //private static string FindLocalPlayerID()
    //{
    //  return MetaPort.Instance.ownerId;
    //  //return CVRPlayerManager.Instance.NetworkPlayers.Where((CVRPlayerEntity playerEntity) => playerEntity != null && playerEntity.PuppetMaster != null).Select((CVRPlayerEntity entity) => entity)
    //}
  }

  [HarmonyPatch]
  internal class HarmonyPatches
  {
    [HarmonyPostfix]
    [HarmonyPatch(typeof(CVRPortableCamera), nameof(CVRPortableCamera.ChangeCameraFilter))]
    public static void After_CVRPortableCamera_ChangeCameraFilter(int f)
    {
      CameraMod.CurrentCameraFilter = f;
    }
  }
}