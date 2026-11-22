"""Read only the plugin's current per-profile status mapping; never game memory."""
import ctypes
import json
import os
import struct


def mapping_name(ini):
    # Match GetFullPathNameW's lexical normalization, including paths beneath
    # junctions; resolving the target would choose a different mapping name.
    absolute = os.path.abspath(ini)
    folded = ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in absolute)
    value = 1469598103934665603
    for byte in folded.encode('utf-16-le'):
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f'Local\\D2RPCVR-RenderDistanceStatus-{value:016x}'


def read_status(ini):
    if os.name != 'nt':
        raise OSError('Live status requires Windows.')
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenFileMappingW.argtypes = (ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p)
    api.OpenFileMappingW.restype = ctypes.c_void_p
    api.MapViewOfFile.argtypes = (ctypes.c_void_p, ctypes.c_uint32,
                                  ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t)
    api.MapViewOfFile.restype = ctypes.c_void_p
    api.UnmapViewOfFile.argtypes = (ctypes.c_void_p,)
    api.CloseHandle.argtypes = (ctypes.c_void_p,)
    api.GetTickCount64.restype = ctypes.c_uint64
    handle = api.OpenFileMappingW(4, False, mapping_name(ini))  # FILE_MAP_READ
    if not handle:
        raise OSError('No live status for this profile. Start the new test DLL.')
    view = None
    try:
        view = api.MapViewOfFile(handle, 4, 0, 0, 4096)
        if not view:
            raise OSError('Live status mapping could not be opened.')
        for _ in range(4):
            before = ctypes.c_uint32.from_address(view).value
            if not before or before & 1:
                continue
            raw = ctypes.string_at(view, 4096)
            after = ctypes.c_uint32.from_address(view).value
            if before == after == struct.unpack_from('<I', raw)[0]:
                status = json.loads(raw[4:].split(b'\0', 1)[0])
                if status.get('schema') != 1:
                    raise ValueError('Unsupported live status version.')
                age = api.GetTickCount64() - status['tick_ms']
                if not 0 <= age <= 3000:
                    raise ValueError('Live status is stale; the game is not updating it.')
                return status
        raise ValueError('Live status is updating; waiting for the next sample.')
    finally:
        if view:
            api.UnmapViewOfFile(view)
        api.CloseHandle(handle)


def format_status(status):
    ready = lambda key: 'ready' if status[key] else 'waiting'
    states = ('not requested', 'waiting for tiling/append bindings',
              'waiting for game-version address', 'invalid game version',
              'waiting for hero room', 'walking room graph')
    state = status['expansion_state']
    expansion = states[state] if 0 <= state < len(states) else 'unknown'
    actual = status['active_model_radius']
    radius = f'{actual:g}' if status['radius_ready'] and actual >= 0 else 'unavailable'
    age = status['tick_ms'] - status['last_expansion_ms']
    activity = 'recent' if status['last_expansion_ms'] and 0 <= age <= 3000 else 'no recent expansion'
    return (f"Live PID {status['pid']} ({'in world' if status['in_world'] else 'menu'}): "
            f"requested rings {status['requested_rings']}; object radius requested "
            f"{status['requested_model_radius']:g}, actual {radius}\n"
            f"Room hooks {ready('room_hooks')}; tiler {ready('tiler_ready')}; "
            f"append {ready('append_ready')}; recalculate {ready('recalculate_ready')}\n"
            f"Expansion: {expansion} ({activity}); furthest room depth {status['furthest_room']}\n"
            f"Added last pass {status['rooms_added']}; tiled successfully {status['rooms_loaded']}; "
            f"tiling attempts {status['tile_attempts']}")
