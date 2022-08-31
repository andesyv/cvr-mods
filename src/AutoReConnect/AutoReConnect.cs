using ABI_RC.Core.Networking.API.Responses;
using ABI_RC.Core.Networking.API;
using ABI_RC.Core.Networking.IO.Instancing;
using BepInEx;
using System.Collections;
using System.Threading.Tasks;
using HopLib;
using System;
using ABI_RC.Core.Savior;
using System.Collections.Generic;
using UnityEngine;
using BepInEx.Configuration;
using HarmonyLib;
using ABI_RC.Core.Networking;

namespace AutoReConnect;

[BepInPlugin(MyPluginInfo.PLUGIN_GUID, MyPluginInfo.PLUGIN_NAME, MyPluginInfo.PLUGIN_VERSION)]
[BepInProcess("ChilloutVR.exe")]
[BepInDependency("xyz.ljoonal.cvr.hoplib", "0.1.0")]
public class AutoReConnect : BaseUnityPlugin
{
    private Dictionary<string, bool> _coroutineRunning = new Dictionary<string, bool>();
    private static InstanceDetails? _instanceDetails = null;
    private bool _isJoining = false;

    private ConfigEntry<bool> _enabled;
    private ConfigEntry<float> _reconnectDelay;
    private ConfigEntry<int> _maxReconnectionAttempts;

    private void Awake()
    {
        SetupConfigs();

        if (!_enabled.Value)
            return;

        var OnInstanceJoined = ToEventHandler(() =>
        {
            _isJoining = false;
            StartOrRestart("FetchInstanceDetailsCoroutine");
            Logger.LogDebug("Connected to instance");

            var gameNetworkTraverser = Traverse.Create(NetworkManager.Instance.GameNetwork);
            _connectionInfo = new ConnectionInfo
            {
                address = NetworkManager.Instance.GameNetwork.Address,
                port = gameNetworkTraverser.Field("port").GetValue<ushort>(),
                ipVersion = gameNetworkTraverser.Field("ipVersion").GetValue<DarkRift.IPVersion>()
            };
        });
        var OnInstanceJoiningStarted = ToEventHandler<InstanceEventArgs>(() =>
        {
            _isJoining = true;
            Logger.LogDebug("Connecting...");
        });
        var OnInstanceDisconnected = ToEventHandler(() =>
        {
            if (_coroutineRunning["FetchInstanceDetailsCoroutine"])
                Stop("FetchInstanceDetailsCoroutine");

            StartOrSkip("TryReconnectCoroutine");
            Logger.LogDebug("Disconnected.");
        });


        var Init = () =>
        {
            HopApi.InstanceJoined += OnInstanceJoined;
            HopApi.InstanceJoiningStarted += OnInstanceJoiningStarted;
            HopApi.InstanceDisconnect += OnInstanceDisconnected;

            StopAllCoroutines();
            _coroutineRunning.TryAdd("FetchInstanceDetailsCoroutine", false);
            _coroutineRunning.TryAdd("TryReconnectCoroutine", false);
        };

        var DeInit = () =>
        {
            HopApi.InstanceJoined -= OnInstanceJoined;
            HopApi.InstanceJoiningStarted -= OnInstanceJoiningStarted;
            HopApi.InstanceDisconnect -= OnInstanceDisconnected;
        };

        // Enable mod to be enabled/disabled on runtime
        _enabled.SettingChanged += ToEventHandler(() =>
        {
            if (_enabled.Value)
                Init();
            else
                DeInit();
        });

        Init();

        Logger.LogInfo($"{MyPluginInfo.PLUGIN_NAME} finished setup");
    }

    void SetupConfigs()
    {
        _enabled = Config.Bind("General", "Enabled", true, "Whether the mod is enabled");
        _reconnectDelay = Config.Bind("General", "ReconnectDelay", 1F, "The delay in seconds to wait after being disconnecting before trying to reconnect again (minimum 0.1 seconds)");
        _maxReconnectionAttempts = Config.Bind("General", "MaxReconnectAttempts", -1, "The maximum allowed times the mod will try to reconnect (negative number for infinite reconnection attempts)");
    }

    private static EventHandler ToEventHandler(Action func)
    {
        return (object sender, System.EventArgs e) => { func(); };
    }
    private static EventHandler<Args> ToEventHandler<Args>(Action func)
    {
        return (object sender, Args e) => { func(); };
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

    private struct ConnectionInfo
    {
        public System.Net.IPAddress address;
        public ushort port;
        public DarkRift.IPVersion ipVersion;
    }

    private ConnectionInfo? _connectionInfo;

    private void StartOrRestart(string coroutine)
    {
        if (_coroutineRunning[coroutine])
            StopCoroutine(coroutine);
        StartCoroutine(coroutine);
    }

    private void StartOrSkip(string coroutine)
    {
        if (_coroutineRunning[coroutine])
            return;
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
        yield return new WaitForSeconds(Math.Max(_reconnectDelay.Value, 0.1F));

        Logger.LogDebug("Attempting to reconnect...");

        var gameNetworkTraverser = Traverse.Create(NetworkManager.Instance.GameNetwork);
        if (gameNetworkTraverser.Field("autoConnect").GetValue<bool>())
        {
            Logger.LogDebug("Network client can autoconnect. Will try that first.");
            gameNetworkTraverser.Method("Start").GetValue();
        }
        else if (_connectionInfo != null)
        {
            Logger.LogDebug("Network client cannot autoconnect. Will try to reconnect automatically");
            NetworkManager.Instance.GameNetwork.Connect(_connectionInfo.Value.address, _connectionInfo.Value.port, _connectionInfo.Value.ipVersion);
        }

        //var tasK = Traverse.Create(typeof(ApiConnection)).Method("Reconnect").GetValue<Task>();
        //while (!tasK.IsCompleted)
        //    yield return null;

        Logger.LogDebug("Established connection with API?");

        yield return new WaitForSeconds(5F);

        // If we're currently not joining an instance and hasn't changed instance since last time, we will try to reconnect
        if (!_isJoining && cachedInstanceDetails != null && InstanceDetails.Equals(cachedInstanceDetails, _instanceDetails))
            Instances.SetJoinTarget(cachedInstanceDetails.Value.instanceId, cachedInstanceDetails.Value.worldId);

        Logger.LogDebug("Starting joining?");

        _coroutineRunning["TryReconnectCoroutine"] = false;
    }

    private static async Task<InstanceDetails?> FetchCurrentInstanceDetails()
    {
        return !string.IsNullOrEmpty(MetaPort.Instance.CurrentInstanceId) ? await FetchInstanceDetails(MetaPort.Instance.CurrentInstanceId) : null;
    }

    private static async Task<InstanceDetails?> FetchInstanceDetails(string InstanceId)
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
