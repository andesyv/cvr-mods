using ABI_RC.Core.IO;
using ABI_RC.Systems.Camera;
using ABI_RC.Systems.ModNetwork;
using MelonLoader;
using System;
using System.Reflection;
using UnityEngine;

/*
 * Layer dump:
 * 
 * Layer 0: Default
 * Layer 1: TransparentFX
 * Layer 2: Ignore Raycast
 * Layer 3: 
 * Layer 4: Water
 * Layer 5: UI
 * Layer 6: PassPlayer
 * Layer 7: BlockPlayer
 * Layer 8: PlayerLocal
 * Layer 9: PlayerClone
 * Layer 10: PlayerNetwork
 * Layer 11: MirrorReflection
 * Layer 12: Camera Only
 * Layer 13: CVRReserved2
 * Layer 14: CVRReserved3
 * Layer 15: UI Internal
 * Layer 16: CVRContent1
 * Layer 17: CVRContent2
 * Layer 18: CVRContent3
 * Layer 19: CVRContent4
 * Layer 20: CVRContent5
 * Layer 21: CVRContent6
 * Layer 22: CVRContent7
 * Layer 23: CVRContent8
 * Layer 24: CVRContent9
 * Layer 25: CVRContent10
 * Layer 26: CVRContent11
 * Layer 27: CVRContent12
 * Layer 28: CVRContent13
 * Layer 29: CVRContent14
 * Layer 30: CVRContent15
 */

namespace VideoCalls
{
  public class CameraMod : ICameraVisualMod
  {
    private const string CameraLayer = "PlayerLocal";

    private VideoCalls mManagerInstance;
    private Camera mCamera;
    private Transmitter mTransmitter;
    //private int mPreviousCullingMask;
    //private LayerMask mLocalPlayerOnlyLayerMask;
    private int mLocalPlayerOnlyFilter = 0;

    public static int CurrentCameraFilter = 0;
    private int mPreviousCameraFilter;

    public CameraMod(VideoCalls managerInstance) => mManagerInstance = managerInstance;

    public string GetModName(string language) => "Video Call";

    public bool ActiveIsOrange() => true;

    public bool DefaultIsOn() => false;

    public int GetSortingOrder() => 11;

    public Sprite GetModImage() => Sprite.Create(Texture2D.redTexture, new Rect(0F, 0F, 4F, 4F), Vector2.zero);
    
    public void Setup(PortableCamera camera, Camera cameraComponent)
    {
      MelonLogger.Msg("VideoCallerCameraMod setup called!");
      mCamera = cameraComponent;
      mTransmitter = mCamera.gameObject.AddComponent<Transmitter>();
      //mManagerInstance.SetupCamera(cameraComponent);

      //var field = camera.GetType().GetField("localPlayerOnly", BindingFlags.NonPublic | BindingFlags.Instance);
      //if (field != null)
      //  mLocalPlayerOnlyLayerMask = (LayerMask)field.GetValue(camera);
      //else
      //  mLocalPlayerOnlyLayerMask = (1 << LayerMask.NameToLayer(CameraLayer));

      var enumValue = typeof(CVRPortableCamera).GetNestedType("CameraFilter", BindingFlags.NonPublic)?.GetField("LocalPlayerOnly")?.GetValue(null);
      if (enumValue == null)
      {
        MelonLogger.Error("Failed to fetch LocalPlayerOnly enum member field");
        //MelonLogger.Error("Failed to fetch CVRPortableCamera.CameraFilter type");
        return;
      }

      //var field = enumType.GetField("LocalPlayerOnly", BindingFlags.Static | BindingFlags.Public);
      //if (field == null)
      //{
      //  return;
      //}

      mLocalPlayerOnlyFilter = (int)enumValue;
    }

    public void Disable()
    {
      if (!CVRPortableCamera.Instance)
      {
        MelonLogger.Error("CameraMod: Disable was attempted, but CVRPortableCamera.Instance is empty!");
        return;
      }

      MelonLogger.Msg("CameraMod: disabled!");

      CVRPortableCamera.Instance.ChangeCameraFilter(mPreviousCameraFilter);

      mManagerInstance.Disconnect();
      mTransmitter.Disable();
    }

    public void Enable()
    {
      if (!CVRPortableCamera.Instance)
      {
        MelonLogger.Error("CameraMod: Enable was attempted, but CVRPortableCamera.Instance is empty!");
        return;
      }

      MelonLogger.Msg("CameraMod: enabled!");

      mPreviousCameraFilter = CurrentCameraFilter;
      CVRPortableCamera.Instance.ChangeCameraFilter(mLocalPlayerOnlyFilter);

      mManagerInstance.Connect();
      mTransmitter.Enable();
    }
  }
}