# Adapted from natpoh/diablo2-vr-port (MIT, Copyright (c) 2026 natpoh) at 91cb1e8; see THIRD_PARTY.md.
# Port of its shared/granny_remesh.h (the Granny 2 reader, writer and the ceiling's mesh makers), line by line,
# with C++'s float32 rounding kept where it decides a byte (the golden test compares the bytes).
"""Read, edit and write a game Granny 2 (v7, LE64) model: the game-drawn ceiling's slabs.

The file: a 32-byte magic, a 72-byte header, 44 bytes a section, the sections' data, then their fixups (each
pointer in a section: where it is, the section and offset it points at). Sections are read raw or BitKnit2
(vendor/bitknit.py, MIT) and always written uncompressed: BitKnit2 streams of raw quanta made the game refuse
the file ("generic bad file"). Edits are found by the file's own type tree, by member name.
"""
import math
import struct
import zlib
from types import SimpleNamespace

from vendor.bitknit import _classic_bitknit_state7_candidates, decode_bitknit_state7_stream, decompress_bitknit

MAGIC_BYTES, HEADER_AT, SECTIONS_AT = 32, 32, 32 + 72
BITKNIT2 = 4
SECTION_LIMIT = 64 << 20
_HEADER = struct.Struct('<18I')      # version fileSize crc sectionTable numSections rootType(2) rootObj(2) typeTag extra[4] stringsCrc reserved[3]
_SECTION = struct.Struct('<11I')     # compression offset compressedLen uncompressedLen alignment first16 first8 fixupsOffset numFixups marshalOffset numMarshal
_FIXUP = struct.Struct('<3I')        # at, section, offset
VERTEX = struct.Struct('<4h4b4b2H')  # int16 x4 position, int8 x4 normal and tangent, half x2 uv (20 bytes)


def f32(v):
    """A double rounded to the nearest float32, as C++'s (float) cast."""
    return struct.unpack('<f', struct.pack('<f', v))[0]


def lround(v):
    """C's lround: half away from zero."""
    return int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))


def _decode(data, size, compression):
    if not 0 <= size <= SECTION_LIMIT:
        raise ValueError('a section is too large')
    if not size:
        return b''
    if compression == 0:
        if len(data) < size:
            raise ValueError('a section lies outside the file')
        return bytes(data[:size])
    if compression != BITKNIT2:
        raise ValueError("a section's compression is not BitKnit2 or none")
    section = SimpleNamespace(index=0, compression=BITKNIT2, expanded_size=size, data_size=len(data),
                              first_16bit=size, first_8bit=size, compression_name='BitKnit2')
    result = decompress_bitknit(section, bytes(data))
    if len(result) != size:
        raise ValueError('a section did not decompress (BitKnit2)')
    return result


class Sections:
    """A file's sections, each decompressed the first time it is used, and only as far as it is read (a model's
    bounds sit some 30 KB into its first section, before its vertices): a list of bytearrays to the writer."""

    STEP = 4096

    def __init__(self, data, sh):
        self._data, self._sh, self._cache, self._prefix = data, sh, {}, {}
        for s in sh:
            compression, offset, packed, size = s[0], s[1], s[2], s[3]
            if size > SECTION_LIMIT or offset + packed > len(data) or (compression == 0 and offset + size > len(data)):
                raise ValueError('a section lies outside the file')
            if compression not in (0, BITKNIT2):
                raise ValueError("a section's compression is not BitKnit2 or none")

    def __len__(self):
        return len(self._sh)

    def _packed(self, i):
        compression, offset, packed, size = self._sh[i][:4]
        return self._data[offset:offset + (size if compression == 0 else packed)]

    def __getitem__(self, i):
        """Section `i` whole (decompressed in full)."""
        if not 0 <= i < len(self._sh):
            raise IndexError(i)
        if i not in self._cache:
            compression, _, _, size = self._sh[i][:4]
            self._cache[i] = bytearray(_decode(self._packed(i), size, compression))
            self._prefix.pop(i, None)
        return self._cache[i]

    def view(self, i, end):
        """Section `i` decompressed at least to `end` (or whole). The BitKnit2 stream is decoded up to a stop a little
        past `end`; what it gives back before the stop is the section's own (a match running past it ends the
        decode there), and a model's mesh lies within its first 40 KB or so."""
        if i in self._cache:
            return self._cache[i]
        compression, _, _, size = self._sh[i][:4]
        have = self._prefix.get(i)
        if compression == 0 or end >= size:
            return self[i]
        if have is not None and len(have) >= end:
            return have
        stop = max(end + self.STEP, 2 * len(have) if have is not None else 0, 40000)
        if stop < size:
            for candidate in _classic_bitknit_state7_candidates(self._packed(i)):
                try:
                    out = decode_bitknit_state7_stream(candidate, size, max_output_size=stop).output
                except (EOFError, ValueError):
                    continue
                if len(out) >= end:
                    self._prefix[i] = bytearray(out)
                    return self._prefix[i]
        return self[i]

    def __iter__(self):
        return (self[i] for i in range(len(self)))

    def size(self, i):
        return len(self._cache[i]) if i in self._cache else self._sh[i][3]


class File:
    """A Granny file read: its magic, header (a list of 18 words), section headers (lists of 11 words), each
    section's bytes (decompressed when first used, editable) and its fixups ([at, section, offset] lists, in the
    file's order)."""

    def __init__(self, data):
        data = bytes(data)
        if len(data) < SECTIONS_AT:
            raise ValueError('too short for a Granny file')
        self.magic = data[:MAGIC_BYTES]
        self.h = list(_HEADER.unpack_from(data, HEADER_AT))
        count = self.h[4]
        if self.h[0] != 7 or count == 0 or count > 32:
            raise ValueError('not a Granny 2 file of version 7')
        table = HEADER_AT + self.h[3]
        if table + count * _SECTION.size > len(data):
            raise ValueError('its section table is cut short')
        self.sh = [list(_SECTION.unpack_from(data, table + i * _SECTION.size)) for i in range(count)]
        self.sec = Sections(data, self.sh)
        self.fix = []
        for s in self.sh:
            compression, at, n = s[0], s[7], s[8]
            fixups = []
            if n:
                if compression == BITKNIT2:
                    if at + 4 > len(data):
                        raise ValueError("a section's fixups lie outside the file")
                    clen, = struct.unpack_from('<I', data, at)
                    if at + 4 + clen > len(data):
                        raise ValueError("a section's fixups lie outside the file")
                    raw = _decode(data[at + 4:at + 4 + clen], n * _FIXUP.size, BITKNIT2)
                else:
                    if at + n * _FIXUP.size > len(data):
                        raise ValueError("a section's fixups lie outside the file")
                    raw = data[at:at + n * _FIXUP.size]
                fixups = [list(x) for x in _FIXUP.iter_unpack(raw)]
            self.fix.append(fixups)
        for i, fixups in enumerate(self.fix):
            size = self.sec.size(i)
            for at, section, _ in fixups:
                if at + 8 > size or section >= count:
                    raise ValueError('a fixup points outside the file')
        if self.h[5] >= count or self.h[7] >= count:
            raise ValueError('its root is outside the file')

    @property
    def root_type(self):
        return (self.h[5], self.h[6])

    @property
    def root_obj(self):
        return (self.h[7], self.h[8])


def at(a, d):
    return None if a is None else (a[0], a[1] + d)


class Walk:
    """The file walked by its own types (an address: (section, offset), None when not found)."""

    def __init__(self, f, shapes=None):
        self.f = f
        self.map = [{x[0]: k for k, x in enumerate(fx)} for fx in f.fix]
        self.shapes = shapes   # struct_names' learnt names, shared between files (None: read every name)

    def inside(self, a, n):
        return a is not None and a[0] < len(self.f.sec) and a[1] + n <= self._size(a[0])

    def _size(self, s):
        sec = self.f.sec
        return sec.size(s) if isinstance(sec, Sections) else len(sec[s])

    def _view(self, s, end):
        sec = self.f.sec
        return sec.view(s, end) if isinstance(sec, Sections) else sec[s]

    def get(self, a, fmt):
        n = struct.calcsize(fmt)
        if not self.inside(a, n):
            return 0
        return struct.unpack_from(fmt, self._view(a[0], a[1] + n), a[1])[0]

    def put(self, a, fmt, v):
        n = struct.calcsize(fmt)
        if not self.inside(a, n):
            return False
        struct.pack_into(fmt, self.f.sec[a[0]], a[1], v)
        return True

    def ptr(self, a):
        if a is None or a[0] >= len(self.map):
            return None
        k = self.map[a[0]].get(a[1])
        if k is None:
            return None
        x = self.f.fix[a[0]][k]
        return (x[1], x[2])

    def set_ptr(self, a, to):
        """The pointer at `a` set to `to` (its fixup changed, or one added)."""
        k = self.map[a[0]].get(a[1])
        if k is not None:
            self.f.fix[a[0]][k][1:] = [to[0], to[1]]
            return
        self.f.fix[a[0]].append([a[1], to[0], to[1]])
        self.map[a[0]][a[1]] = len(self.f.fix[a[0]]) - 1
        self.put(a, '<Q', 0)

    def string(self, a):
        if a is None or a[0] >= len(self.f.sec):
            return ''
        want = a[1] + 256
        while True:
            data = self._view(a[0], want)
            end = data.find(b'\0', a[1])
            if end >= 0 or len(data) >= self._size(a[0]):
                return bytes(data[a[1]:len(data) if end < 0 else end]).decode('latin-1')
            want = 2 * len(data)

    def append(self, s, data, align=8):
        """Bytes appended to a section (aligned): their place."""
        v = self.f.sec[s]
        while len(v) % align:
            v.append(0)
        a = (s, len(v))
        v.extend(data)
        return a

    # a type entry (44 bytes): its kind, name, the struct it refers to, its width (array count)
    def kind(self, t):
        return self.get(t, '<i')

    def name(self, t):
        return self.string(self.ptr(at(t, 4)))

    def ref(self, t):
        return self.ptr(at(t, 12))

    def width(self, t):
        return self.get(at(t, 20), '<i')

    def size(self, t, depth=0):
        w = self.width(t) or 1
        k = self.kind(t)
        if k == 1:
            return self.struct_size(self.ref(t), depth + 1) * w
        if k in (2, 8, 22):
            return 8 * w
        if k in (3, 4):
            return 12 * w
        if k == 5:
            return 16 * w
        if k == 7:
            return 20 * w
        if k == 9:
            return 68 * w
        if k in (10, 19, 20):
            return 4 * w
        if k in (11, 12, 13, 14):
            return w
        if k in (15, 16, 17, 18, 21):
            return 2 * w
        return 0

    def struct_size(self, t, depth=0):
        n = 0
        for _ in range(256):
            if depth >= 32 or not self.inside(t, 44) or self.kind(t) == 0:
                break
            n += self.size(t, depth)
            t = at(t, 44)
        return n

    def struct_names(self, t):
        """The members' names of the struct of type `t`, from `shapes` when a struct of its very shape was named before:
        its entries' bytes but the pointers, and how far apart their names lie (the names' lengths). The names are
        strings at the end of the first section, which only a whole decode reaches: learnt once a shape."""
        entries, places = [], []
        for _ in range(256):
            if not self.inside(t, 44) or self.kind(t) == 0:
                break
            raw = self._view(t[0], t[1] + 44)
            entries.append(bytes(raw[t[1]:t[1] + 4]) + bytes(raw[t[1] + 20:t[1] + 44]))
            places.append(self.ptr(at(t, 4)))
            t = at(t, 44)
        if not places or None in places:
            return None
        # (a name the file shares with a string elsewhere - "Name", "FromFileName" - lies anywhere: only the block of the
        # struct's own names, within 4 KB of its last, keys it)
        last = places[-1]
        key = (tuple(entries), tuple(p[1] - last[1] if p[0] == last[0] and abs(p[1] - last[1]) <= 4096 else None for p in places))
        if key not in self.shapes:
            self.shapes[key] = [self.string(p) for p in places]
        return self.shapes[key]

    def member(self, t, obj, name):
        """A member of a struct of type `t` at `obj`: (its place, its type entry); (None, None) when not found."""
        names = self.struct_names(t) if self.shapes is not None else None
        if names is not None:
            off = 0
            for known in names:
                if known == name:
                    return at(obj, off), t
                off += self.size(t)
                t = at(t, 44)
            return None, None
        off = 0
        for _ in range(256):
            if not self.inside(t, 44) or self.kind(t) == 0:
                break
            if self.name(t) == name:
                return at(obj, off), t
            off += self.size(t)
            t = at(t, 44)
        return None, None


def write(f):
    """The file as the game's are laid out: magic, header, section table, every section, then every section's
    fixups; no marshalling; every section stored as it is (compression 0)."""
    n = len(f.sec)
    sh = [list(s) for s in f.sh]
    data = [bytes(s) for s in f.sec]
    fixups = [b''.join(_FIXUP.pack(*x) for x in fx) for fx in f.fix]
    pos = SECTIONS_AT + n * _SECTION.size
    out = bytearray(pos)
    for i in range(n):
        while pos % 4:
            out.append(0)
            pos += 1
        sh[i][0] = 0
        sh[i][1] = pos
        sh[i][2] = len(data[i])
        sh[i][3] = len(f.sec[i])
        sh[i][5] = min(sh[i][5], sh[i][3])
        sh[i][6] = min(sh[i][6], sh[i][3])
        out += data[i]
        pos += len(data[i])
    for i in range(n):
        while pos % 4:
            out.append(0)
            pos += 1
        sh[i][7] = pos
        sh[i][8] = len(f.fix[i])
        out += fixups[i]
        pos += len(fixups[i])
        sh[i][9] = pos
        sh[i][10] = 0
    h = list(f.h)
    h[4] = n
    h[3] = SECTIONS_AT - HEADER_AT
    h[1] = len(out)
    out[0:MAGIC_BYTES] = f.magic
    struct.pack_into('<I', out, 16, SECTIONS_AT + n * _SECTION.size)   # the magic's header size: to the section table's end
    for i in range(n):
        _SECTION.pack_into(out, SECTIONS_AT + i * _SECTION.size, *sh[i])
    h[2] = 0
    _HEADER.pack_into(out, HEADER_AT, *h)
    h[2] = zlib.crc32(bytes(out[SECTIONS_AT:])) & 0xFFFFFFFF   # the game's rule: from +104 to the end
    _HEADER.pack_into(out, HEADER_AT, *h)
    return bytes(out)


# ---- meshes ------------------------------------------------------------------------------------------------------------------

def half(v):
    """A float32 as a half, as the source's Half (round half up, not to even)."""
    x, = struct.unpack('<I', struct.pack('<f', v))
    s = (x >> 16) & 0x8000
    e = ((x >> 23) & 0xFF) - 127 + 15
    m = x & 0x7FFFFF
    if e <= 0:
        if e < -10:
            return s
        m = (m | 0x800000) >> (1 - e)
        return (s | ((m + 0x1000) >> 13)) & 0xFFFF
    if e >= 31:
        return s | 0x7C00
    r = (e << 10) | (m >> 13)
    return (s + r + ((m >> 12) & 1)) & 0xFFFF


def snorm8(v):
    """A float32 in -1 .. 1 as int8, C++'s lround(v * 127.0f)."""
    v = f32(v)
    v = -1.0 if v < -1.0 else 1.0 if v > 1.0 else v
    return lround(f32(v * 127.0))


class Mesh:
    def __init__(self):
        self.v = []          # vertices: [pos(4), normal(4), tangent(4), uv(2)] lists
        self.idx = []        # triangles, both windings
        self.scale = 0.0     # VertexScale (float32): a position is int16 / 32767 times it
        self.mn = [1e30, 1e30, 1e30]
        self.mx = [-1e30, -1e30, -1e30]

    def vertex_bytes(self):
        return b''.join(VERTEX.pack(*v[0], *v[1], *v[2], *v[3]) for v in self.v)

    def index_bytes(self):
        return struct.pack(f'<{len(self.idx)}H', *self.idx)


def _w(scale):
    # (std::log2 of a float is a float)
    return lround(f32(math.log2(scale)) / 13.0 * 32767.0)


def _bound(m, p):
    for k in range(3):
        if p[k] < m.mn[k]:
            m.mn[k] = f32(p[k])
        if p[k] > m.mx[k]:
            m.mx[k] = f32(p[k])


def salt(m, salt):
    """Each piece's mesh its own bytes (the game shares a model it already has by what it holds): `salt` more
    vertices nobody points at (copies of the first, their uv far off), its triangles begun `salt` on."""
    if not salt or not m.v or len(m.idx) < 3:
        return
    for k in range(salt):
        if len(m.v) >= 65536:
            break
        p, n, t, _ = m.v[0]
        m.v.append([list(p), list(n), list(t), [half(1000.0 + salt), half(float(k))]])
    tris = len(m.idx) // 3
    by = salt % tris
    m.idx = m.idx[by * 3:] + m.idx[:by * 3]


def ceiling_relief(u, v):
    """The rock's underside in a tile, u and v in tiles: 0 (flat at the top) .. 1 (hanging down the most)."""
    k2pi = 6.28318530717958647692
    h = (0.35 * math.sin(k2pi * (u + 0.13)) * math.cos(k2pi * (v - 0.21)) + 0.25 * math.sin(k2pi * (2.0 * u + v) + 1.3) +
         0.15 * math.cos(k2pi * (u - 3.0 * v) + 0.4) + 0.10 * math.sin(k2pi * (3.0 * u + 2.0 * v) + 2.1))
    return 0.5 + 0.5 * h / 0.85


def ceiling_mesh(tile, tiles_half, cells, relief):
    """A square slab round the origin, `tiles_half` tiles of `tile` units each way, `cells` cells a tile: its
    underside from y 0 down to -relief, seen from below (normals down, both windings), one picture a tile."""
    tile, relief = f32(tile), f32(relief)
    m = Mesh()
    n = 2 * tiles_half * cells
    row = n + 1
    half_span, step = tile * tiles_half, tile / cells
    m.scale = f32(half_span if half_span > relief else relief)
    w = _w(m.scale)

    def y(x, z):
        return -relief * ceiling_relief(x / tile, -z / tile)

    e = step * 0.25
    for i in range(n + 1):
        for j in range(n + 1):
            x, z = -half_span + j * step, -half_span + i * step
            py = y(x, z)
            dx = (y(x + e, z) - y(x - e, z)) / (2.0 * e)
            dz = (y(x, z + e) - y(x, z - e)) / (2.0 * e)
            nl = math.sqrt(dx * dx + 1.0 + dz * dz)
            tl = math.sqrt(1.0 + dx * dx)
            p = (x, py, z)
            pos = [lround(p[k] / m.scale * 32767.0) for k in range(3)] + [w]
            _bound(m, p)
            normal = [snorm8(dx / nl), snorm8(-1.0 / nl), snorm8(dz / nl), 0]
            tangent = [snorm8(1.0 / tl), snorm8(dx / tl), 0, -127]
            m.v.append([pos, normal, tangent, [half(f32(x / tile)), half(f32(-z / tile))]])
    for i in range(n):
        for j in range(n):
            a, b, c, d = i * row + j, i * row + j + 1, (i + 1) * row + j, (i + 1) * row + j + 1
            for t in ((a, b, c), (b, d, c)):
                m.idx += [t[0], t[1], t[2], t[0], t[2], t[1]]
    return m


def wall_mesh(radius, height, segments, rows, tile):
    """A ring of rock round the origin, `radius` round, from y 0 down to -height, seen from inside (normals in,
    both windings); a whole number of pictures round it, u round, v up."""
    radius, height, tile = f32(radius), f32(height), f32(tile)
    k2pi = 6.28318530717958647692
    m = Mesh()
    m.scale = radius if radius > height else height
    w = _w(m.scale)
    around = math.floor(k2pi * radius / tile + 0.5)
    around = 1.0 if around < 1.0 else around
    for i in range(rows + 1):
        for j in range(segments + 1):
            a = k2pi * j / segments
            s, c = math.sin(a), math.cos(a)
            p = (s * radius, -height * i / rows, c * radius)
            pos = [lround(p[k] / m.scale * 32767.0) for k in range(3)] + [w]
            _bound(m, p)
            normal = [snorm8(-s), 0, snorm8(-c), 0]
            tangent = [snorm8(c), 0, snorm8(-s), -127]
            m.v.append([pos, normal, tangent, [half(f32(around * j / segments)), half(f32(p[1] / tile))]])
    row = segments + 1
    for i in range(rows):
        for j in range(segments):
            a, b, c, d = i * row + j, i * row + j + 1, (i + 1) * row + j, (i + 1) * row + j + 1
            for t in ((a, b, c), (b, d, c)):
                m.idx += [t[0], t[1], t[2], t[0], t[2], t[1]]
    return m


# ---- a model of the game's read: its bounds (the walls' top) -----------------------------------------------------------------

def _mesh_ext(g, mesh_type, mesh):
    ext, t = g.member(mesh_type, mesh, 'ExtendedData')
    if ext is None or g.kind(t) != 5:
        return None, None
    et, eo = g.ptr(ext), g.ptr(at(ext, 8))
    return (et, eo) if et is not None and eo is not None else (None, None)


def bounds(f, shapes=None):
    """Every mesh's bounds together (ExtendedData bbMin / bbMax), model units; None when it has none. `shapes`: the
    structs named so far ({}: shared from file to file, so that only the start of the first section is decoded once
    each struct's names are known)."""
    g = Walk(f, shapes)
    meshes, t = g.member(f.root_type, f.root_obj, 'Meshes')
    if meshes is None or g.kind(t) != 4:
        return None
    mesh_type, arr = g.ref(t), g.ptr(at(meshes, 4))
    mn, mx, any_ = [0.0] * 3, [0.0] * 3, False
    for k in range(min(g.get(meshes, '<i'), 4096)):
        et, eo = _mesh_ext(g, mesh_type, g.ptr(at(arr, k * 8)))
        if et is None:
            continue
        lo, _ = g.member(et, eo, 'bbMin')
        hi, _ = g.member(et, eo, 'bbMax')
        if lo is None or hi is None:
            continue
        for c in range(3):
            a, b = g.get(at(lo, c * 4), '<f'), g.get(at(hi, c * 4), '<f')
            if not any_ or a < mn[c]:
                mn[c] = a
            if not any_ or b > mx[c]:
                mx[c] = b
        any_ = True
    return (mn, mx) if any_ else None


# ---- the edits -----------------------------------------------------------------------------------------------------------------

def remesh(f, m, textures, lod):
    """`f` (a game model, read) takes mesh `m`: each texture's new name wherever the file names the old one
    (every one must be there), its skeletons' bones identity, LOD `lod`. Raises ValueError when the file is not
    laid out as the rock's is (nothing half done is written by the caller)."""
    g = Walk(f)
    rt, ro = f.root_type, f.root_obj
    meshes, t = g.member(rt, ro, 'Meshes')
    if meshes is None or g.kind(t) != 4 or g.get(meshes, '<i') < 1:
        raise ValueError('no mesh')
    mesh_type, mesh = g.ref(t), g.ptr(g.ptr(at(meshes, 4)))
    if mesh is None:
        raise ValueError('no mesh')
    pvd_at, t = g.member(mesh_type, mesh, 'PrimaryVertexData')
    pvd = g.ptr(pvd_at)
    if pvd is None:
        raise ValueError('no vertex data')
    verts, t = g.member(g.ref(t), pvd, 'Vertices')
    if verts is None or g.kind(t) != 7:
        raise ValueError('no vertex array')
    vt = g.ptr(verts)
    if vt is None or g.struct_size(vt) != VERTEX.size:
        raise ValueError("the vertex type is not the card's (20 bytes)")
    for name, kind, width in (('Position', 17, 4), ('Normal', 13, 4), ('Tangent', 13, 4), ('TextureCoordinates0', 21, 2)):
        a, e = g.member(vt, vt, name)
        if a is None or g.kind(e) != kind or g.width(e) != width:
            raise ValueError("the vertex type's members are not the card's")
    vdata = g.append(0, m.vertex_bytes(), 4)
    g.put(at(verts, 8), '<i', len(m.v))
    g.set_ptr(at(verts, 12), vdata)
    tris = len(m.idx) // 3
    idata = g.append(0, m.index_bytes(), 4)
    topos = []

    def edit_topo(topo, topo_type):
        if topo in topos:
            return True
        topos.append(topo)
        groups, gt = g.member(topo_type, topo, 'Groups')
        i16, it16 = g.member(topo_type, topo, 'Indices16')
        i32, _ = g.member(topo_type, topo, 'Indices')
        if groups is None or g.kind(gt) != 3 or i16 is None or g.kind(it16) != 3 or g.get(groups, '<i') < 1:
            return False
        if i32 is not None and g.get(i32, '<i') != 0:
            return False
        g0, group_type = g.ptr(at(groups, 4)), g.ref(gt)
        first, _ = g.member(group_type, g0, 'TriFirst')
        count, _ = g.member(group_type, g0, 'TriCount')
        mat, _ = g.member(group_type, g0, 'MaterialIndex')
        if first is None or count is None:
            return False
        g.put(groups, '<i', 1)
        g.put(first, '<i', 0)
        g.put(count, '<i', tris)
        if mat is not None:
            g.put(mat, '<i', 0)
        g.put(i16, '<i', len(m.idx))
        g.set_ptr(at(i16, 4), idata)
        return True

    pt, t = g.member(mesh_type, mesh, 'PrimaryTopology')
    if pt is None or not edit_topo(g.ptr(pt), g.ref(t)):
        raise ValueError("the mesh's topology is not the card's")
    tts, t = g.member(rt, ro, 'TriTopologies')
    if tts is not None and g.kind(t) == 4:
        for k in range(g.get(tts, '<i')):
            tp = g.ptr(at(g.ptr(at(tts, 4)), k * 8))
            if tp is not None and not edit_topo(tp, g.ref(t)):
                raise ValueError('a topology is not the card\'s')
    # the mesh's boxes and scale
    bb, t = g.member(mesh_type, mesh, 'BoneBindings')
    if bb is not None and g.kind(t) == 3:
        bb_type, arr = g.ref(t), g.ptr(at(bb, 4))
        size = g.struct_size(bb_type)
        for k in range(g.get(bb, '<i')):
            one = at(arr, k * size)
            lo, _ = g.member(bb_type, one, 'OBBMin')
            hi, _ = g.member(bb_type, one, 'OBBMax')
            if lo is not None:
                for c in range(3):
                    g.put(at(lo, c * 4), '<f', m.mn[c])
            if hi is not None:
                for c in range(3):
                    g.put(at(hi, c * 4), '<f', m.mx[c])
    ext, t = g.member(mesh_type, mesh, 'ExtendedData')
    if ext is None or g.kind(t) != 5:
        raise ValueError('the mesh has no extended data')
    et, eo = g.ptr(ext), g.ptr(at(ext, 8))
    lo, _ = g.member(et, eo, 'bbMin')
    hi, _ = g.member(et, eo, 'bbMax')
    vs, _ = g.member(et, eo, 'VertexScale')
    if vs is None:
        raise ValueError('the mesh has no VertexScale')
    if lo is not None:
        for c in range(3):
            g.put(at(lo, c * 4), '<f', m.mn[c])
    if hi is not None:
        for c in range(3):
            g.put(at(hi, c * 4), '<f', m.mx[c])
    g.put(vs, '<f', m.scale)
    # every skeleton's bones: identity
    seen = []

    def bones(sk, sk_type):
        if sk in seen:
            return
        seen.append(sk)
        arr, bt = g.member(sk_type, sk, 'Bones')
        if arr is None or g.kind(bt) != 3:
            return
        bone_type, data = g.ref(bt), g.ptr(at(arr, 4))
        size = g.struct_size(bone_type)
        for k in range(g.get(arr, '<i')):
            one = at(data, k * size)
            tr, _ = g.member(bone_type, one, 'Transform')
            inv, _ = g.member(bone_type, one, 'InverseWorldTransform')
            if tr is not None:
                ident = (0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1)   # pos, rot (x y z w), scale / shear 3 x 3
                g.put(tr, '<I', 0)
                for c in range(16):
                    g.put(at(tr, 4 + c * 4), '<f', ident[c])
            if inv is not None:
                for c in range(16):
                    g.put(at(inv, c * 4), '<f', 1.0 if c % 5 == 0 else 0.0)

    sks, t = g.member(rt, ro, 'Skeletons')
    if sks is not None and g.kind(t) == 4:
        for k in range(g.get(sks, '<i')):
            bones(g.ptr(at(g.ptr(at(sks, 4)), k * 8)), g.ref(t))
    models, t = g.member(rt, ro, 'Models')
    if models is not None and g.kind(t) == 4 and g.get(models, '<i') >= 1:
        model_type, model = g.ref(t), g.ptr(g.ptr(at(models, 4)))
        sk, e = g.member(model_type, model, 'Skeleton')
        if sk is not None:
            bones(g.ptr(sk), g.ref(e))
        mext, e = g.member(model_type, model, 'ExtendedData')
        if mext is not None:
            lod_at, _ = g.member(g.ptr(mext), g.ptr(at(mext, 8)), 'lodLevel')
            if lod_at is not None:
                g.put(lod_at, '<i', lod)
    # the textures' names
    for old, new in textures:
        string = g.append(0, new.encode('latin-1') + b'\0', 4)
        named = 0
        for s in range(len(f.fix)):
            for x in f.fix[s]:
                if g.string((x[1], x[2])) == old:
                    x[1], x[2] = string
                    named += 1
        if not named:
            raise ValueError('a texture name to change is not in the file')
