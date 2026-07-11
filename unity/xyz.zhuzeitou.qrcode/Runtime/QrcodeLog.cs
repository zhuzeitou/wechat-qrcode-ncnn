using System;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using ZZT.QRCode.Native;

namespace ZZT.QRCode
{
    public static class QrcodeLog
    {
        // ─── Runtime log sink public API ────────────────────────────────────
        //
        // This facade owns one runtime sink only while managed listeners exist.
        // Its level is sink-local: it never changes another native consumer.

        public enum LogLevel
        {
            Verbose = 0,
            Debug = 1,
            Info = 2,
            Warn = 3,
            Error = 4,
        }

        public static QrcodeResults.ErrorCode SetMinimumLogLevel(LogLevel minLevel)
        {
            int numericLevel = (int)minLevel;
            if (numericLevel < (int)LogLevel.Verbose || numericLevel > (int)LogLevel.Error)
                return QrcodeResults.ErrorCode.ErrorInvalidArgument;

            lock (LogMessageLock)
            {
                _desiredLevel = numericLevel;
                ++_desiredLevelGeneration;
            }
            return (QrcodeResults.ErrorCode)ReconcileLogSink(false);
        }

        public delegate void LogMessageHandler(LogLevel level, string message);

        private enum CompletionOwner
        {
            Initiator,
            DestroyCallback
        }

        private sealed class NativeSinkLifetime
        {
            internal readonly ulong Generation;
            internal readonly ulong TransitionToken;
            internal GCHandle Handle;
            internal bool Destroyed;
            internal bool HandleFreed;

            internal NativeSinkLifetime(ulong generation, ulong transitionToken)
            {
                Generation = generation;
                TransitionToken = transitionToken;
            }
        }

        private static readonly object LogMessageLock = new();
        private static LogMessageHandler _directHandlers;
        private static LogMessageHandler _mainThreadHandlers;
        private static bool _bridgeLogSubscribed;
        private static bool _desiredEnabled;
        private static bool _actualEnabled;
        private static int _desiredLevel = (int)LogLevel.Warn;
        private static int _actualLevel = (int)LogLevel.Warn;
        private static ulong _sinkId;
        private static ulong _desiredEnabledGeneration;
        private static ulong _desiredLevelGeneration;
        private static ulong _transitionToken;
        private static ulong _lifetimeGeneration;
        private static ulong _appliedLevelGeneration;
        private static ulong _transitionDesiredEnabledGeneration;
        private static ulong _transitionDesiredLevelGeneration;
        private static ulong _transitionTargetId;
        private static int _transitionTargetLevel;
        private static bool _transitioning;
        private static bool _unloading;
        private static int _lastLogError;
        private static NativeSinkLifetime _lifetime;
        private static CompletionOwner _completionOwner;

        [ThreadStatic] private static int _nativeLogCallbackDepth;
        [ThreadStatic] private static bool _callbackNeedsReconcile;

        private static int _mainThreadId;
        private static SynchronizationContext _mainThreadContext;
        private static int _epoch;

        private static bool HasHandlersLocked() => _directHandlers != null || _mainThreadHandlers != null;

        private static void SubscribeBridgeIfNeeded()
        {
            if (_bridgeLogSubscribed) return;
            Bridge.LogMessageReceived += OnBridgeLogMessage;
            _bridgeLogSubscribed = true;
        }

        private static void UnsubscribeBridgeIfNoHandlers()
        {
            if (HasHandlersLocked()) return;
            if (_bridgeLogSubscribed)
            {
                Bridge.LogMessageReceived -= OnBridgeLogMessage;
                _bridgeLogSubscribed = false;
            }
        }

        private static void MutateEnabledForHandlersLocked()
        {
            bool enabled = !_unloading && HasHandlersLocked();
            if (_desiredEnabled == enabled) return;
            _desiredEnabled = enabled;
            ++_desiredEnabledGeneration;
        }

        public static event LogMessageHandler OnLogMessage
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers += value;
                    SubscribeBridgeIfNeeded();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
        }

        public static event LogMessageHandler OnLogMessageMainThread
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers += value;
                    SubscribeBridgeIfNeeded();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
        }

        private static void RequestLogSinkReconcile()
        {
            if (_nativeLogCallbackDepth != 0)
            {
                _callbackNeedsReconcile = true;
                return;
            }
            ReconcileLogSink(false);
        }

        // A single caller owns each native transition. Other external callers wait;
        // a callback only records new desired state, avoiding callback/remove cycles.
        private static int ReconcileLogSink(bool callbackOrigin)
        {
            NativeSinkLifetime lifetime = null;
            ulong token = 0;
            ulong sinkId = 0;
            int targetLevel = 0;
            int action = 0; // 1 add, 2 update, 3 remove

            lock (LogMessageLock)
            {
                while (_transitioning)
                {
                    if (_nativeLogCallbackDepth != 0) return _lastLogError;
                    Monitor.Wait(LogMessageLock);
                }

                if (_desiredEnabled && !_actualEnabled)
                {
                    action = 1;
                    targetLevel = _desiredLevel;
                    token = ++_transitionToken;
                    lifetime = new NativeSinkLifetime(++_lifetimeGeneration, token);
                    try
                    {
                        lifetime.Handle = GCHandle.Alloc(lifetime);
                    }
                    catch
                    {
                        _lastLogError = (int)QrcodeResults.ErrorCode.ErrorOutOfMemory;
                        return _lastLogError;
                    }
                }
                else if (_desiredEnabled && _actualEnabled && _desiredLevel != _actualLevel)
                {
                    action = 2;
                    token = ++_transitionToken;
                    sinkId = _sinkId;
                    targetLevel = _desiredLevel;
                }
                else if (!_desiredEnabled && _actualEnabled)
                {
                    action = 3;
                    token = ++_transitionToken;
                    sinkId = _sinkId;
                    lifetime = _lifetime;
                    _completionOwner = callbackOrigin
                        ? CompletionOwner.DestroyCallback
                        : CompletionOwner.Initiator;
                }
                else
                {
                    return _lastLogError;
                }

                _transitioning = true;
                if (action != 3) _completionOwner = CompletionOwner.Initiator;
                _transitionDesiredEnabledGeneration = _desiredEnabledGeneration;
                _transitionDesiredLevelGeneration = _desiredLevelGeneration;
                _transitionTargetId = sinkId;
                _transitionTargetLevel = targetLevel;
            }

            int error;
            if (action == 1)
            {
                IntPtr userData = GCHandle.ToIntPtr(lifetime.Handle);
                Bridge.NativeLogSinkOptions options = Bridge.CreateLogSinkOptions(userData, targetLevel);
                error = Bridge.AddRuntimeLogSink(ref options, out sinkId);
                if (error != 0)
                {
                    lifetime.Handle.Free();
                    lock (LogMessageLock)
                    {
                        if (_transitioning && token == _transitionToken)
                        {
                            _lastLogError = error;
                            _transitioning = false;
                            Monitor.PulseAll(LogMessageLock);
                        }
                    }
                    return error;
                }
            }
            else if (action == 2)
            {
                error = Bridge.SetRuntimeLogSinkLevel(sinkId, targetLevel);
            }
            else
            {
                error = Bridge.RemoveRuntimeLogSink(sinkId);
            }

            bool reconcileAgain;
            lock (LogMessageLock)
            {
                if (token != _transitionToken) return error;

                if (action == 1)
                {
                    if (error == 0)
                    {
                        _actualEnabled = true;
                        _actualLevel = targetLevel;
                        _appliedLevelGeneration = _transitionDesiredLevelGeneration;
                        _sinkId = sinkId;
                        _lifetime = lifetime;
                        _lastLogError = 0;
                    }
                    else
                    {
                        _lastLogError = error;
                    }
                    _transitioning = false;
                    Monitor.PulseAll(LogMessageLock);
                }
                else if (action == 2)
                {
                    if (error == 0)
                    {
                        _actualLevel = targetLevel;
                        _appliedLevelGeneration = _transitionDesiredLevelGeneration;
                        _lastLogError = 0;
                    }
                    else
                    {
                        _lastLogError = error;
                    }
                    _transitioning = false;
                    Monitor.PulseAll(LogMessageLock);
                }
                else if (_completionOwner == CompletionOwner.Initiator)
                {
                    if ((error == 0 ||
                         error == (int)QrcodeResults.ErrorCode.ErrorInvalidHandle) &&
                        lifetime != null && lifetime.Destroyed)
                    {
                        CommitDestroyedLocked(lifetime, token);
                    }
                    else
                    {
                        _lastLogError = error == 0 ||
                            error == (int)QrcodeResults.ErrorCode.ErrorInvalidHandle
                            ? (int)QrcodeResults.ErrorCode.ErrorInternal
                            : error;
                        _transitioning = false;
                        Monitor.PulseAll(LogMessageLock);
                    }
                }

                reconcileAgain = !_transitioning &&
                    ((_desiredEnabled && (!_actualEnabled || _desiredLevel != _actualLevel)) ||
                     (!_desiredEnabled && _actualEnabled));
            }

            if (reconcileAgain && error == 0) ReconcileLogSink(false);
            return error;
        }

        internal static void EnterNativeLogCallback()
        {
            ++_nativeLogCallbackDepth;
        }

        internal static void ExitNativeLogCallback()
        {
            if (--_nativeLogCallbackDepth != 0 || !_callbackNeedsReconcile) return;
            _callbackNeedsReconcile = false;
            ReconcileLogSink(true);
        }

        internal static void OnNativeLogSinkDestroyed(IntPtr userData)
        {
            NativeSinkLifetime lifetime = userData == IntPtr.Zero
                ? null
                : GCHandle.FromIntPtr(userData).Target as NativeSinkLifetime;
            if (lifetime == null) return;

            lock (LogMessageLock)
            {
                lifetime.Destroyed = true;
                if (_lifetime != lifetime) return;
                if (_completionOwner == CompletionOwner.DestroyCallback)
                    CommitDestroyedLocked(lifetime, _transitionToken);
            }
        }

        internal static void FinalizeNativeLogSinkLifetime(IntPtr userData)
        {
            if (userData == IntPtr.Zero) return;
            GCHandle handle = GCHandle.FromIntPtr(userData);
            NativeSinkLifetime lifetime = handle.Target as NativeSinkLifetime;
            if (lifetime == null || lifetime.HandleFreed) return;
            lifetime.HandleFreed = true;
            handle.Free();
        }

        private static void CommitDestroyedLocked(NativeSinkLifetime lifetime, ulong token)
        {
            if (token != _transitionToken || _lifetime != lifetime) return;
            _actualEnabled = false;
            _sinkId = 0;
            _lifetime = null;
            _lastLogError = 0;
            _transitioning = false;
            Monitor.PulseAll(LogMessageLock);
        }

        private static void OnBridgeLogMessage(int level, string message)
        {
            LogMessageHandler directSnapshot;
            LogMessageHandler mainSnapshot;
            int epochAtCapture;

            lock (LogMessageLock)
            {
                directSnapshot = _directHandlers;
                mainSnapshot = _mainThreadHandlers;
                epochAtCapture = _epoch;
            }

            if (directSnapshot != null)
            {
                foreach (LogMessageHandler handler in directSnapshot.GetInvocationList())
                {
                    try { handler((LogLevel)level, message); }
                    catch { }
                }
            }

            if (mainSnapshot == null) return;
            if (Environment.CurrentManagedThreadId == _mainThreadId)
            {
                InvokeMainSnapshot(mainSnapshot, level, message);
                return;
            }
            if (_mainThreadContext == null) return;

            var capturedMessage = message;
            var capturedLevel = level;
            var capturedEpoch = epochAtCapture;
            var capturedSnapshot = mainSnapshot;
            _mainThreadContext.Post(_ =>
            {
                if (_epoch != capturedEpoch) return;
                InvokeMainSnapshot(capturedSnapshot, capturedLevel, capturedMessage);
            }, null);
        }

        private static void InvokeMainSnapshot(LogMessageHandler snapshot, int level, string message)
        {
            foreach (LogMessageHandler handler in snapshot.GetInvocationList())
            {
                try { handler((LogLevel)level, message); }
                catch { }
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void OnSubsystemRegistration()
        {
            ClearLogCallbackState();
            lock (LogMessageLock)
            {
                _unloading = false;
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterAssembliesLoaded)]
        private static void AfterAssembliesLoaded()
        {
            _mainThreadId = Environment.CurrentManagedThreadId;
            _mainThreadContext = SynchronizationContext.Current;
            Application.quitting -= OnApplicationQuitting;
            Application.quitting += OnApplicationQuitting;
        }

        private static void OnApplicationQuitting()
        {
            ClearLogCallbackState();
        }

        private static void ClearLogCallbackState()
        {
            lock (LogMessageLock)
            {
                _unloading = true;
                _directHandlers = null;
                _mainThreadHandlers = null;
                UnsubscribeBridgeIfNoHandlers();
                MutateEnabledForHandlersLocked();
            }
            ReconcileLogSink(false);
            Interlocked.Increment(ref _epoch);
        }
    }
}
