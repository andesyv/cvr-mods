using ABI_RC.Systems.ModNetwork;
using MelonLoader;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace VideoCalls
{
  public class Transmitter : MonoBehaviour
  {
    private Camera mCamera;
    private const float mTransmitInterval = 1.0f;
    private float mTime = 0F;
    private List<AsyncGPUReadbackRequest> mPendingRequests = new();
    private bool mEnabled = false;
    private RenderTextureFormat mFormat;

    private void Start()
    {
      mCamera = GetComponent<Camera>();
      if (!mCamera)
        MelonLogger.Error("Cannot find camera");
    }

    private void Update()
    {
      if (!mCamera || !mEnabled)
        return;

      mTime += Time.deltaTime;
      if (mTransmitInterval > mTime)
        return;

      mTime -= mTransmitInterval;

      // If the format has suddenly changed, we need to invalidate the pending requests as
      // the requested data is outdated
      if (mCamera.targetTexture.format != mFormat)
      {
        mFormat = mCamera.targetTexture.format;
        mPendingRequests.Clear();
        MelonLogger.Msg("Transmitter: Updated format");
      }

      // Process pending and current readback requests
      List<AsyncGPUReadbackRequest> pendingRequests = new();
      pendingRequests.Capacity = mPendingRequests.Capacity + 1;
      foreach (var request in mPendingRequests)
      {
        if (request.done)
        {
          MelonLogger.Msg("Transmitter: Processing done image");
          using var msg = new ModNetworkMessage(VideoCalls.ModNetworkId);
          msg.Write(VideoCalls.MessageType.ConnectionData);

          var data = request.GetData<byte>();
          msg.Write(new VideoCalls.ImageData(request.width, request.height, data.ToArray(), mFormat));
          msg.Send();

          MelonLogger.Msg("Transmitter: Sent an image through the mod network");
        }
        else
        {
          pendingRequests.Add(request);
        }
      }
      pendingRequests.Add(AsyncGPUReadback.Request(mCamera.targetTexture));
      mPendingRequests = pendingRequests;
    }

    public void Enable()
    {
      MelonLogger.Msg("Transmitter enabled!");
      mEnabled = true;
    }

    public void Disable()
    {
      MelonLogger.Msg("Transmitter disabled!");
      mPendingRequests.Clear();
      mTime = 0F;
      mEnabled = false;
    }
  }
}