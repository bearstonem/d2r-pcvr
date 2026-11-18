"""Read-only Battle.net CASC adapter for the pinned Unicode x64 CascLib 3.0.

Only local storage is opened, without online/download flags. Asset caches stay
in the owner's installation; no archive, account or save files are written.
"""
import ctypes as C
from pathlib import Path
from steam_assets import MAX_FILE, canonical

DWORD = C.c_uint32
HANDLE = C.c_void_p
BOOL = C.c_bool  # CascLib exports C++ bool, not Win32 BOOL.


class FindData(C.Structure):
    _fields_ = [('name', C.c_char * 260), ('ckey', C.c_ubyte * 16),
                ('ekey', C.c_ubyte * 16), ('tags', C.c_uint64),
                ('size', C.c_uint64), ('plain_name', HANDLE),
                ('file_id', DWORD), ('locale', DWORD), ('content', DWORD),
                ('spans', DWORD), ('available', DWORD), ('name_type', DWORD)]


class CascAssets:
    def __init__(self, game, library):
        from game_assets import build_hash
        self.build_hash = build_hash(game)
        self.handle = HANDLE()
        self.files = {}
        library = Path(library).resolve()
        if not library.is_file():
            raise ValueError(f'Battle.net asset reading requires the packaged CascLib.dll: {library}')
        if C.sizeof(HANDLE) != 8 or C.sizeof(FindData) != 344:
            raise ValueError('CascLib requires the 64-bit Windows Python runtime.')
        # Absolute path + safe DLL search: never load a DLL from the game or cwd.
        self.dll = C.WinDLL(str(library), winmode=0x1100)
        signatures = {
            'CascOpenStorage': (BOOL, [C.c_wchar_p, DWORD, C.POINTER(HANDLE)]),
            'CascCloseStorage': (BOOL, [HANDLE]),
            'CascOpenFile': (BOOL, [HANDLE, C.c_char_p, DWORD, DWORD, C.POINTER(HANDLE)]),
            'CascGetFileSize64': (BOOL, [HANDLE, C.POINTER(C.c_uint64)]),
            'CascReadFile': (BOOL, [HANDLE, HANDLE, DWORD, C.POINTER(DWORD)]),
            'CascCloseFile': (BOOL, [HANDLE]),
            'CascFindFirstFile': (HANDLE, [HANDLE, C.c_char_p, C.POINTER(FindData), C.c_wchar_p]),
            'CascFindNextFile': (BOOL, [HANDLE, C.POINTER(FindData)]),
            'CascFindClose': (BOOL, [HANDLE]),
            'GetCascError': (DWORD, []),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.dll, name)
            function.restype, function.argtypes = result, arguments
        if not self.dll.CascOpenStorage(str(Path(game).resolve()), 2, C.byref(self.handle)):
            raise self.error('Open local Battle.net archives')
        try:
            self._enumerate()
        except BaseException:
            self.close()
            raise

    def error(self, action):
        return OSError(f'{action} failed (CascLib error {self.dll.GetCascError()}). Complete the Battle.net game download first.')

    def _enumerate(self):
        entry = FindData()
        search = self.dll.CascFindFirstFile(self.handle, b'*', C.byref(entry), None)
        if not search or search == C.c_void_p(-1).value:
            raise self.error('Read archive index')
        try:
            count = 0
            while True:
                count += 1
                if count > 5_000_000:
                    raise ValueError('Battle.net archive index exceeds the file limit')
                if entry.name_type == 0:
                    original = bytes(entry.name)
                    name = canonical(original.decode('utf-8'))
                    # Only what this mod reads: the game's own art and tables. The UI layouts are
                    # here too - a mod overrides one by shipping its path, so the prepared stash
                    # layout is built from this copy (see prepare_collection_tab).
                    if name.startswith(('data/hd/', 'data/global/excel/', 'data/global/ui/layouts/')):
                        self.files[name] = original
                if not self.dll.CascFindNextFile(search, C.byref(entry)):
                    if self.dll.GetCascError() not in (0, 18):  # end of index
                        raise self.error('Read archive index')
                    break
        finally:
            self.dll.CascFindClose(search)
        if not self.files:
            raise ValueError('No named D2R assets found in the local Battle.net archives')

    def read(self, name):
        if not self.handle:
            raise ValueError('Battle.net archive reader is closed')
        name = canonical(name)
        original = self.files[name]
        handle = HANDLE()
        # CASC_OPEN_BY_NAME | CASC_STRICT_DATA_CHECK; no network/download flags.
        if not self.dll.CascOpenFile(self.handle, original, 2, 0x10, C.byref(handle)):
            raise self.error(f'Open {name}')
        try:
            size = C.c_uint64()
            if not self.dll.CascGetFileSize64(handle, C.byref(size)):
                raise self.error(f'Size {name}')
            if size.value > MAX_FILE:
                raise ValueError('Asset exceeds size limit')
            data = C.create_string_buffer(size.value)
            read = DWORD()
            if not self.dll.CascReadFile(handle, data, size.value, C.byref(read)):
                raise self.error(f'Read {name}')
            if read.value != size.value:
                raise ValueError(f'Truncated Battle.net asset: {name}')
            return data.raw
        finally:
            self.dll.CascCloseFile(handle)

    def close(self):
        if self.handle:
            self.dll.CascCloseStorage(self.handle)
            self.handle = HANDLE()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
