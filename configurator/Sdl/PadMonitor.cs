// Enumerates connected SDL gamepads and produces live snapshots.
//
// SDL runs on a DEDICATED background thread: SDL pumps the owning thread's
// Windows message queue, which would re-enter the WPF dispatcher if used on
// the UI thread. The UI reads cached snapshots under a lock.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace AdfConfigurator.Sdl;

public sealed record PadDevice(int Id, string Guid, string Name);

public sealed class PadSnapshot
{
    public bool Connected;
    public float Lx, Ly, Rx, Ry, Lt, Rt;
    public readonly bool[] Buttons = new bool[Sdl3Native.ButtonCount];
}

public sealed class PadMonitor : IDisposable
{
    private sealed class Handle
    {
        public required int Id;
        public required IntPtr Ptr;
        public required string Guid;
        public required string Name;
    }

    private readonly object _lock = new();
    private readonly Thread _thread;
    private readonly IntPtr _eventBuf;
    private volatile bool _stop;
    private volatile string? _preferGuid;

    private readonly List<Handle> _open = new();
    private readonly List<PadDevice> _devices = new();
    private PadSnapshot? _last;

    public bool IsAvailable { get; private set; } = true;
    public string? InitError { get; private set; }

    public PadMonitor()
    {
        _eventBuf = Marshal.AllocHGlobal(128);
        // SDL_Init happens on the worker thread; optimistically mark available
        // and report failure through the first snapshot / device list being empty.
        IsAvailable = true;
        _thread = new Thread(Worker) { Name = "AdfSdlPad", IsBackground = true };
        _thread.Start();
    }

    /// <summary>GUID the worker should snapshot (empty = first pad).</summary>
    public string? PreferGuid
    {
        set => _preferGuid = string.IsNullOrEmpty(value) ? null : value;
    }

    public IReadOnlyList<PadDevice> GetDevices()
    {
        lock (_lock) return _devices.ToArray();
    }

    public PadSnapshot Snapshot()
    {
        lock (_lock)
        {
            var copy = new PadSnapshot();
            if (_last != null)
            {
                copy.Connected = _last.Connected;
                copy.Lx = _last.Lx; copy.Ly = _last.Ly;
                copy.Rx = _last.Rx; copy.Ry = _last.Ry;
                copy.Lt = _last.Lt; copy.Rt = _last.Rt;
                Array.Copy(_last.Buttons, copy.Buttons, copy.Buttons.Length);
            }
            return copy;
        }
    }

    private void Worker()
    {
        if (!Sdl3Native.SDL_Init(Sdl3Native.InitGamepad))
        {
            InitError = Sdl3Native.GetError();
            IsAvailable = false;
            return;
        }

        try
        {
            uint untilReconcile = 0;
            while (!_stop)
            {
                Sdl3Native.SDL_PumpEvents();
                DrainEvents();

                // Cheap clock tick: reconcile the device list periodically.
                uint now = (uint)Environment.TickCount;
                if (unchecked(now - untilReconcile) >= 500)
                {
                    untilReconcile = now;
                    ReconcileDevices();
                }
                UpdateSnapshot();
                Thread.Sleep(16);
            }
        }
        finally
        {
            foreach (var h in _open)
                Sdl3Native.SDL_CloseGamepad(h.Ptr);
            _open.Clear();
            Sdl3Native.SDL_Quit();
        }
    }

    private void DrainEvents()
    {
        while (Sdl3Native.SDL_PollEvent(_eventBuf))
        {
            uint type = (uint)Marshal.ReadInt32(_eventBuf, 0);
            int which = Marshal.ReadInt32(_eventBuf, 16);
            if (type == Sdl3Native.EventGamepadRemoved)
            {
                lock (_lock)
                {
                    var h = _open.Find(x => x.Id == which);
                    if (h != null)
                    {
                        Sdl3Native.SDL_CloseGamepad(h.Ptr);
                        _open.Remove(h);
                    }
                }
            }
        }
    }

    private void ReconcileDevices()
    {
        IntPtr idsPtr = Sdl3Native.SDL_GetGamepads(out int count);
        if (idsPtr == IntPtr.Zero)
        {
            lock (_lock)
            {
                if (_devices.Count != 0) _devices.Clear();
            }
            return;
        }
        try
        {
            lock (_lock)
            {
                bool changed = _devices.Count != count;
                for (int i = 0; i < count; i++)
                {
                    int id = Marshal.ReadInt32(idsPtr, i * 4);
                    if (_open.Exists(h => h.Id == id)) continue;

                    IntPtr ptr = Sdl3Native.SDL_OpenGamepad(id);
                    if (ptr == IntPtr.Zero)
                        ptr = Sdl3Native.SDL_GetGamepadFromID(id);
                    if (ptr == IntPtr.Zero) continue;

                    string guid = Sdl3Native.GuidToString(
                        Sdl3Native.SDL_GetGamepadGUIDForID(id));
                    var h = new Handle
                    {
                        Id = id,
                        Ptr = ptr,
                        Guid = guid,
                        Name = Sdl3Native.GamepadName(ptr),
                    };
                    _open.Add(h);
                    changed = true;
                }
                if (changed)
                {
                    _devices.Clear();
                    foreach (var h in _open)
                        _devices.Add(new PadDevice(h.Id, h.Guid, h.Name));
                }
            }
        }
        finally
        {
            Sdl3Native.SDL_free(idsPtr);
        }
    }

    private void UpdateSnapshot()
    {
        lock (_lock)
        {
            var s = new PadSnapshot();
            if (_open.Count > 0)
            {
                Handle? chosen = null;
                string? prefer = _preferGuid;
                if (!string.IsNullOrEmpty(prefer))
                    chosen = _open.Find(h => h.Guid == prefer);
                chosen ??= _open[0];

                s.Connected = true;
                float Norm(short v)
                {
                    float f = Math.Clamp(v / 32768.0f, -1f, 1f);
                    return MathF.Abs(f) < 0.001f ? 0f : f;
                }

                s.Lx = Norm(Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisLeftX));
                s.Ly = -Norm(Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisLeftY));
                s.Rx = Norm(Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisRightX));
                s.Ry = -Norm(Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisRightY));
                s.Lt = Math.Clamp(
                    Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisLeftTrigger) /
                    32767.0f, 0f, 1f);
                s.Rt = Math.Clamp(
                    Sdl3Native.SDL_GetGamepadAxis(chosen.Ptr, Sdl3Native.AxisRightTrigger) /
                    32767.0f, 0f, 1f);
                for (int i = 0; i < Sdl3Native.ButtonCount; i++)
                    s.Buttons[i] = Sdl3Native.SDL_GetGamepadButton(chosen.Ptr, i);
            }
            _last = s;
        }
    }

    public void Dispose()
    {
        _stop = true;
        if (Thread.CurrentThread != _thread)
            _thread.Join(2000);
        Marshal.FreeHGlobal(_eventBuf);
    }
}
