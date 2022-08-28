using ABI_RC.Core.InteractionSystem;
using ABI_RC.Core.Networking;
using ABI_RC.Core.Networking.API.Responses;
using ABI_RC.Core.Networking.API;
using ABI_RC.Core.Networking.IO.Instancing;
using BepInEx;
using BepInEx.Unity.Mono;
using HarmonyLib;
using System.Collections;
using System.Threading.Tasks;
using HopLib;
using System;
using ABI_RC.Core.Savior;
using System.Collections.Generic;
using UnityEngine;

namespace AutoReConnect;

[BepInPlugin(MyPluginInfo.PLUGIN_GUID, MyPluginInfo.PLUGIN_NAME, MyPluginInfo.PLUGIN_VERSION)]
[BepInProcess("ChilloutVR.exe")]
[BepInDependency("xyz.ljoonal.cvr.hoplib", "0.1.0")]
public class AutoReConnect : BaseUnityPlugin
{
    private Dictionary<string, bool> _coroutineRunning = new Dictionary<string, bool>();
    private static InstanceDetails? _instanceDetails = null;
    private bool _isJoining = false;

    private void Awake()
    {
        // Plugin startup logic
        Logger.LogInfo($"Plugin {MyPluginInfo.PLUGIN_GUID} is loaded!");


        HopApi.InstanceJoined += delegate
        {
            _isJoining = false;
            StartOrRestart("FetchInstanceDetailsCoroutine");
        };

        HopApi.InstanceJoiningStarted += delegate
        {
            _isJoining = true;
        };

        HopApi.InstanceDisconnect += delegate
        {
            if (_coroutineRunning["FetchInstanceDetailsCoroutine"])
                Stop("FetchInstanceDetailsCoroutine");

            StartOrRestart("TryReconnectCoroutine");
        };
    }

    public struct InstanceDetails
    {
        public string instanceId;
        public string worldId;
        public long timestamp;

        public static bool Equals(InstanceDetails lhs, InstanceDetails rhs) =>
            lhs.instanceId == rhs.instanceId && lhs.worldId == rhs.worldId && lhs.timestamp != rhs.timestamp;
        public static bool Equals(InstanceDetails? lhs, InstanceDetails? rhs) =>
            lhs != null && rhs != null && Equals(lhs ?? new InstanceDetails { }, rhs ?? new InstanceDetails { });
    }

    private void StartOrRestart(string coroutine)
    {
        if (_coroutineRunning[coroutine])
            StopCoroutine(coroutine);
        StartCoroutine(coroutine);
    }

    private void Stop(string coroutine)
    {
        if (_coroutineRunning[coroutine])
            StopCoroutine(coroutine);
        _coroutineRunning[coroutine] = false;
    }

    private IEnumerator FetchInstanceDetailsCoroutine()
    {
        _coroutineRunning["FetchInstanceDetailsCoroutine"] = true;
        var task = FetchCurrentInstanceDetails();
        while (!task.IsCompleted)
            yield return null;
        _instanceDetails = task.Result;
        Logger.LogDebug($"Current instance is ${_instanceDetails?.instanceId ?? "unknown"}");
        _coroutineRunning["FetchInstanceDetailsCoroutine"] = false;
    }

    private IEnumerator TryReconnectCoroutine()
    {
        var cachedInstanceDetails = _instanceDetails;
        _coroutineRunning["TryReconnectCoroutine"] = true;
        yield return new WaitForSeconds(5f);

        // If we're currently not joining an instance and hasn't changed instance since last time, we will try to reconnect
        if (!_isJoining && cachedInstanceDetails != null && InstanceDetails.Equals(cachedInstanceDetails, _instanceDetails))
            Instances.SetJoinTarget(cachedInstanceDetails.Value.instanceId, cachedInstanceDetails.Value.worldId);

        _coroutineRunning["TryReconnectCoroutine"] = false;
    }

    private async Task<InstanceDetails?> FetchCurrentInstanceDetails()
    {
        return !string.IsNullOrEmpty(MetaPort.Instance.CurrentInstanceId) ? await FetchInstanceDetails(MetaPort.Instance.CurrentInstanceId) : null;
    }

    private async Task<InstanceDetails?> FetchInstanceDetails(string InstanceId)
    {
        BaseResponse<InstanceDetailsResponse> baseResponse = await ApiConnection.MakeRequest<InstanceDetailsResponse>(ApiConnection.ApiOperation.InstanceDetail, new
        {
            instanceID = InstanceId
        });
        if (baseResponse == null)
        {
            return null;
        }

        return new InstanceDetails
        {
            instanceId = baseResponse.Data.Id,
            worldId = baseResponse.Data.World.Id,
            timestamp = CreateTimestamp()
        };
    }

    private static long CreateTimestamp()
    {
        return ((DateTimeOffset)DateTime.Now).ToUnixTimeSeconds();
    }
}
