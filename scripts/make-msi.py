"""Builds the distributable MSI, out/LBP-810-driver-<version>.msi, from the signed driver package.

Run after build.cmd and scripts/make-package.ps1 (release.cmd runs all three). The result is one
32-bit MSI for Windows 7 SP1 .. 11, x86 and x64. It copies out/package to
"Program Files\\LBP-810 Driver\\driver" and runs LBP810CA.DLL (src/setup) built for the OS
architecture, which trusts the certificate, installs the driver and creates the printer queue;
uninstalling removes all of it again. A newer MSI upgrades an older one (the queue is kept).

The database is written through msi.dll, so no WiX is needed. The MSI UI is Windows Installer's
basic progress dialog; messages of the custom actions are Russian, like the driver UI.
"""
import ctypes
import hashlib
import os
import re
import struct
import subprocess
import sys
import time
import uuid
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = ROOT / 'out' / 'package'
SETUP = ROOT / 'out' / 'setup'
WORK = ROOT / 'build' / 'msi'

UPGRADE_CODE = '{4CE4BC3F-DCA9-4C22-B1F3-5B3075543A85}'  # never change: links all versions
COMPONENT_NS = uuid.UUID('d82a5040-38f4-41ab-b08a-e6f0b205bd65')  # component GUID = uuid5(ns, name)
PRODUCT_NAME = 'Драйвер принтера Canon LBP-810'
MANUFACTURER = 'relbp810 project'
CODEPAGE = 1251
LANGUAGE = 1049
CABINET = 'driver.cab'
ARCHS = {'amd64': 'x64', 'x86': 'x86'}  # package folder -> custom action suffix

# Standard tables, in the notation of MSI .idt files: s/S string (S nullable), l/L localizable,
# i/I integer, v binary stream, followed by the width; * marks primary key columns.
SCHEMA = {
    'Property': '*Property s72, Value l0',
    'Directory': '*Directory s72, Directory_Parent S72, DefaultDir l255',
    'Component': '*Component s72, ComponentId S38, Directory_ s72, Attributes i2, Condition S255, KeyPath S72',
    'Feature': '*Feature s38, Feature_Parent S38, Title L64, Description L255, Display I2, Level i2, '
               'Directory_ S72, Attributes i2',
    'FeatureComponents': '*Feature_ s38, *Component_ s72',
    'File': '*File s72, Component_ s72, FileName l255, FileSize i4, Version S72, Language S20, '
            'Attributes I2, Sequence i2',
    'Media': '*DiskId i2, LastSequence i2, DiskPrompt L64, Cabinet S255, VolumeLabel S32, Source S72',
    'Binary': '*Name s72, Data v0',
    'CustomAction': '*Action s72, Type i2, Source S72, Target S255',
    'ActionText': '*Action s72, Description L0, Template L0',
    'Upgrade': '*UpgradeCode s38, *VersionMin S20, *VersionMax S20, *Language S255, *Attributes i4, '
               'Remove S255, ActionProperty s72',
    'LaunchCondition': '*Condition s255, Description l255',
    'InstallExecuteSequence': '*Action s72, Condition S255, Sequence I2',
    'InstallUISequence': '*Action s72, Condition S255, Sequence I2',
    'AdminExecuteSequence': '*Action s72, Condition S255, Sequence I2',
    'AdminUISequence': '*Action s72, Condition S255, Sequence I2',
}

# Imports that do not exist on Windows 7 SP1. The current MSVC STL links some of them statically
# (std::mutex -> GetSystemTimePreciseAsFileTime), which makes a DLL fail to load there.
WIN8_IMPORTS = {
    'GetSystemTimePreciseAsFileTime', 'CreateFile2', 'CopyFile2', 'WaitOnAddress', 'WakeByAddressSingle',
    'WakeByAddressAll', 'GetCurrentThreadStackLimits', 'SetThreadDescription', 'GetThreadDescription',
    'PrefetchVirtualMemory', 'GetOverlappedResultEx', 'InitializeSynchronizationBarrier',
    'EnterSynchronizationBarrier', 'DeleteSynchronizationBarrier', 'CreateFileMappingFromApp',
    'MapViewOfFileFromApp', 'VirtualAllocFromApp', 'SetProcessMitigationPolicy', 'GetProcessMitigationPolicy',
    'GetFirmwareType', 'IsWow64Process2', 'GetCurrentPackageId', 'GetCurrentPackageFullName',
    'DiscardVirtualMemory', 'OfferVirtualMemory', 'ReclaimVirtualMemory', 'SetThreadInformation',
    'GetThreadInformation', 'QueryUnbiasedInterruptTimePrecise', 'GetSystemCpuSetInformation',
    'SetDefaultDllDirectories', 'AddDllDirectory', 'RemoveDllDirectory', 'RoInitialize',
}
WIN8_DLLS = re.compile(r'(?i)^(api-ms-win-|ext-ms-win-|vcruntime|msvcp1|ucrtbase|concrt)')


def fail(msg):
    sys.exit(f'make-msi: {msg}')


# --- msi.dll ----------------------------------------------------------------------------------

MSIHANDLE = ctypes.c_ulong
_msi = ctypes.WinDLL('msi')
for name, args, res in [
    ('MsiOpenDatabaseW', [wintypes.LPCWSTR, ctypes.c_void_p, ctypes.POINTER(MSIHANDLE)], ctypes.c_uint),
    ('MsiDatabaseImportW', [MSIHANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR], ctypes.c_uint),
    ('MsiDatabaseOpenViewW', [MSIHANDLE, wintypes.LPCWSTR, ctypes.POINTER(MSIHANDLE)], ctypes.c_uint),
    ('MsiDatabaseCommit', [MSIHANDLE], ctypes.c_uint),
    ('MsiViewExecute', [MSIHANDLE, MSIHANDLE], ctypes.c_uint),
    ('MsiViewClose', [MSIHANDLE], ctypes.c_uint),
    ('MsiCloseHandle', [MSIHANDLE], ctypes.c_uint),
    ('MsiCreateRecord', [ctypes.c_uint], MSIHANDLE),
    ('MsiRecordSetStringW', [MSIHANDLE, ctypes.c_uint, wintypes.LPCWSTR], ctypes.c_uint),
    ('MsiRecordSetInteger', [MSIHANDLE, ctypes.c_uint, ctypes.c_int], ctypes.c_uint),
    ('MsiRecordSetStreamW', [MSIHANDLE, ctypes.c_uint, wintypes.LPCWSTR], ctypes.c_uint),
    ('MsiGetSummaryInformationW', [MSIHANDLE, wintypes.LPCWSTR, ctypes.c_uint, ctypes.POINTER(MSIHANDLE)], ctypes.c_uint),
    ('MsiSummaryInfoSetPropertyW', [MSIHANDLE, ctypes.c_uint, ctypes.c_uint, ctypes.c_int,
                                    ctypes.POINTER(wintypes.FILETIME), wintypes.LPCWSTR], ctypes.c_uint),
    ('MsiSummaryInfoPersist', [MSIHANDLE], ctypes.c_uint),
    ('MsiGetFileVersionW', [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD),
                            wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)], ctypes.c_uint),
    ('MsiGetLastErrorRecord', [], MSIHANDLE),
    ('MsiFormatRecordW', [MSIHANDLE, MSIHANDLE, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)], ctypes.c_uint),
]:
    fn = getattr(_msi, name)
    fn.argtypes, fn.restype = args, res


def check(rc, what):
    if rc == 0:
        return
    detail = ''
    rec = _msi.MsiGetLastErrorRecord()
    if rec:
        buf = ctypes.create_unicode_buffer(1024)
        n = wintypes.DWORD(1024)
        _msi.MsiFormatRecordW(0, rec, buf, ctypes.byref(n))
        _msi.MsiCloseHandle(rec)
        detail = f' [{buf.value}]'
    fail(f'{what}: error {rc}{detail}')


class Stream:
    """A file stored in a binary column."""
    def __init__(self, path):
        self.path = Path(path)


class Database:
    def __init__(self, path):
        self.h = MSIHANDLE()
        check(_msi.MsiOpenDatabaseW(str(path), ctypes.c_void_p(3), ctypes.byref(self.h)), 'MsiOpenDatabase')  # CREATE

    def execute(self, sql, rows=((),)):
        view = MSIHANDLE()
        check(_msi.MsiDatabaseOpenViewW(self.h, sql, ctypes.byref(view)), sql)
        for row in rows:
            rec = _msi.MsiCreateRecord(len(row)) if row else 0
            for i, v in enumerate(row, 1):
                if v is None:
                    continue
                if isinstance(v, int):
                    check(_msi.MsiRecordSetInteger(rec, i, v), f'{sql} {row}')
                elif isinstance(v, Stream):
                    check(_msi.MsiRecordSetStreamW(rec, i, str(v.path)), f'{sql} {row}')
                else:
                    check(_msi.MsiRecordSetStringW(rec, i, v), f'{sql} {row}')
            check(_msi.MsiViewExecute(view, rec), f'{sql} {row}')
            if rec:
                _msi.MsiCloseHandle(rec)
        _msi.MsiViewClose(view)
        _msi.MsiCloseHandle(view)

    def set_codepage(self, codepage):
        idt = WORK / '_ForceCodepage.idt'
        idt.write_bytes(f'\r\n\r\n{codepage}\t_ForceCodepage\r\n'.encode('ascii'))
        check(_msi.MsiDatabaseImportW(self.h, str(WORK), idt.name), 'import _ForceCodepage')

    def create_table(self, table):
        cols, keys = [], []
        for col in SCHEMA[table].split(', '):
            name, typ = col.split()
            if name.startswith('*'):
                name = name[1:]
                keys.append(f'`{name}`')
            kind, width = typ[0], int(typ[1:])
            if kind in 'sSlL':
                sql = 'LONGCHAR' if width == 0 else f'CHAR({width})'
            elif kind in 'iI':
                sql = 'SHORT' if width == 2 else 'LONG'
            else:
                sql = 'OBJECT'
            if kind.islower():
                sql += ' NOT NULL'
            if kind in 'lL':
                sql += ' LOCALIZABLE'
            cols.append(f'`{name}` {sql}')
        self.execute(f'CREATE TABLE `{table}` ({", ".join(cols)} PRIMARY KEY {", ".join(keys)})')

    def insert(self, table, rows):
        names = [c.split()[0].lstrip('*') for c in SCHEMA.get(table, '').split(', ')] if table in SCHEMA else ['Name', 'Data']
        sql = f'INSERT INTO `{table}` ({", ".join(f"`{n}`" for n in names)}) VALUES ({", ".join("?" * len(names))})'
        self.execute(sql, [tuple(r) + (None,) * (len(names) - len(r)) for r in rows])

    def summary(self, props):
        h = MSIHANDLE()
        check(_msi.MsiGetSummaryInformationW(self.h, None, len(props), ctypes.byref(h)), 'MsiGetSummaryInformation')
        for pid, value in props:  # PID_CODEPAGE first: it decides how the strings are stored
            if isinstance(value, int):
                vt = 2 if pid == 1 else 3  # VT_I2 / VT_I4
                check(_msi.MsiSummaryInfoSetPropertyW(h, pid, vt, value, None, None), f'summary {pid}')
            elif isinstance(value, wintypes.FILETIME):
                check(_msi.MsiSummaryInfoSetPropertyW(h, pid, 64, 0, ctypes.byref(value), None), f'summary {pid}')
            else:
                check(_msi.MsiSummaryInfoSetPropertyW(h, pid, 30, 0, None, value), f'summary {pid}')
        check(_msi.MsiSummaryInfoPersist(h), 'MsiSummaryInfoPersist')
        _msi.MsiCloseHandle(h)

    def commit(self):
        check(_msi.MsiDatabaseCommit(self.h), 'MsiDatabaseCommit')
        _msi.MsiCloseHandle(self.h)


def file_version(path):
    ver, lang = ctypes.create_unicode_buffer(64), ctypes.create_unicode_buffer(64)
    nv, nl = wintypes.DWORD(64), wintypes.DWORD(64)
    if _msi.MsiGetFileVersionW(str(path), ver, ctypes.byref(nv), lang, ctypes.byref(nl)) != 0:
        return None, None
    return ver.value, lang.value


# --- checks -----------------------------------------------------------------------------------

def pe_imports(path):
    """{dll: [function names]} from the import directory of a PE32/PE32+ file."""
    d = path.read_bytes()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if d[pe:pe + 4] != b'PE\0\0':
        fail(f'{path} is not a PE file')
    nsec, optsize = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    plus = struct.unpack_from('<H', d, opt)[0] == 0x20B
    imp_rva = struct.unpack_from('<I', d, opt + (112 if plus else 96) + 8)[0]
    sections = [struct.unpack_from('<IIII', d, opt + optsize + 40 * i + 8) for i in range(nsec)]

    def off(rva):
        for vsize, va, rawsize, raw in sections:
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + raw
        fail(f'{path}: bad RVA {rva:#x}')

    def cstr(o):
        return d[o:d.index(b'\0', o)].decode('ascii')

    out = {}
    o = off(imp_rva) if imp_rva else None
    while o is not None:
        lookup, _, _, name_rva, iat = struct.unpack_from('<IIIII', d, o)
        if not name_rva:
            break
        fmt, size, ordinal = ('<Q', 8, 1 << 63) if plus else ('<I', 4, 1 << 31)
        t, names = off(lookup or iat), []
        while (v := struct.unpack_from(fmt, d, t)[0]):
            if not v & ordinal:
                names.append(cstr(off(v & 0x7FFFFFFF) + 2))
            t += size
        out[cstr(off(name_rva))] = names
        o += 20
    return out


def check_win7(paths):
    bad = []
    for p in paths:
        for dll, names in pe_imports(p).items():
            if WIN8_DLLS.match(dll):
                bad.append(f'{p.relative_to(ROOT)}: imports {dll}')
            bad += [f'{p.relative_to(ROOT)}: imports {n} ({dll})' for n in names if n in WIN8_IMPORTS]
    if bad:
        fail('binaries would not load on Windows 7:\n  ' + '\n  '.join(bad))


def versions():
    h = (ROOT / 'src' / 'driver' / 'version.rc.h').read_text()
    v = {k: int(n) for k, n in re.findall(r'#define VER_(MAJOR|MINOR|BUILD|REV) (\d+)', h)}
    driver = f'{v["MAJOR"]}.{v["MINOR"]}.{v["BUILD"]}.{v["REV"]}'
    inf = (PKG / 'CNLB810.INF').read_text(encoding='utf-8', errors='replace')
    m = re.search(r'^DriverVer\s*=\s*[^,]+,\s*([\d.]+)', inf, re.M)
    if not m or m.group(1) != driver:
        fail(f'INF DriverVer {m and m.group(1)} differs from version.rc.h {driver}')
    # Windows Installer compares only three fields: fold build and revision into the third.
    return driver, f'{v["MAJOR"]}.{v["MINOR"]}.{v["BUILD"] * 1000 + v["REV"]}'


def check_package():
    for f in [PKG / 'CNLB810.INF', PKG / 'cnlb810.cat', PKG / 'cnlb810.cer'] + \
             [PKG / a / 'CNLB810L.DLL' for a in ARCHS] + [SETUP / a / 'LBP810CA.DLL' for a in ARCHS]:
        if not f.is_file():
            fail(f'{f.relative_to(ROOT)} is missing: run build.cmd and scripts\\make-package.ps1')
    if (PKG / 'CNLB810.INF').read_bytes() != (ROOT / 'src' / 'driver' / 'inf' / 'CNLB810.INF').read_bytes():
        fail('out\\package\\CNLB810.INF is stale: run scripts\\make-package.ps1')
    cat = (PKG / 'cnlb810.cat').stat().st_mtime
    stale = [f for a in ARCHS for f in (PKG / a).iterdir() if f.stat().st_mtime > cat]
    if stale:
        fail(f'{stale[0].relative_to(ROOT)} is newer than the catalog: run scripts\\make-package.ps1')


def check_version_reuse(driver_ver):
    """Windows keeps the driver store copy of a package whose INF is identical, so a rebuild that
    keeps the version does not reach machines which have the earlier build of that version."""
    digest = hashlib.sha256()
    for f in sorted(p for p in PKG.rglob('*') if p.is_file() and p.suffix.lower() != '.cat'):
        digest.update(f.relative_to(PKG).as_posix().encode() + b'\0' + f.read_bytes())
    stamp = WORK / f'package-{driver_ver}.sha256'
    if stamp.is_file() and stamp.read_text().strip() != digest.hexdigest():
        print(f'make-msi: WARNING: {driver_ver} was packaged before with other driver files; machines that '
              f'installed that build keep its files. Bump VER_REV in src\\driver\\version.rc.h and DriverVer '
              f'in src\\driver\\inf\\CNLB810.INF for every MSI that leaves this machine.')
    WORK.mkdir(parents=True, exist_ok=True)
    stamp.write_text(digest.hexdigest())


# --- database content -------------------------------------------------------------------------

def msi_name(name, used):
    """FileName/DefaultDir value: the name itself if it is 8.3, else 'SHORT~N.EXT|long name'."""
    base, ext = os.path.splitext(name)
    if re.fullmatch(r'[A-Za-z0-9_\-]{1,8}', base) and re.fullmatch(r'(\.[A-Za-z0-9_\-]{1,3})?', ext):
        return name
    stem, n = re.sub(r'[^A-Za-z0-9_]', '', base).upper()[:6] or 'F', 1
    while f'{stem}~{n}{ext[:4]}'.upper() in used:
        n += 1
    used.add(f'{stem}~{n}{ext[:4]}'.upper())
    return f'{stem}~{n}{ext[:4]}'.upper() + '|' + name


def guid(name):
    return '{' + str(uuid.uuid5(COMPONENT_NS, name)).upper() + '}'


def build(out_path, driver_ver, msi_ver):
    product_code = '{' + str(uuid.uuid4()).upper() + '}'
    package_code = '{' + str(uuid.uuid4()).upper() + '}'
    WORK.mkdir(parents=True, exist_ok=True)
    tmp = WORK / out_path.name
    tmp.unlink(missing_ok=True)

    # Files: the driver package as it is, one component per folder.
    folders = [('Package', 'DRIVERDIR', PKG, '')] + [(f'Package_{a}', f'DRIVER_{a}', PKG / a, f'{a}_') for a in ARCHS]
    components, files, cab = [], [], []
    for comp, directory, src, prefix in folders:
        used, keypath = set(), None
        for f in sorted(p for p in src.iterdir() if p.is_file()):
            key = prefix + f.name
            ver, lang = file_version(f)
            files.append((key, comp, msi_name(f.name, used), f.stat().st_size, ver, lang, 512, len(files) + 1))
            cab.append((f, key))
            if keypath is None or f.suffix.upper() == '.INF':
                keypath = key
        components.append((comp, guid(comp), directory, 0, None, keypath))

    ddf = WORK / 'driver.ddf'
    lines = ['.OPTION EXPLICIT', f'.Set CabinetNameTemplate={CABINET}', f'.Set DiskDirectoryTemplate="{WORK}"',
             '.Set Cabinet=on', '.Set Compress=on', '.Set CompressionType=LZX', '.Set CompressionMemory=21',
             '.Set MaxDiskSize=0', '.Set CabinetFileCountThreshold=0', '.Set FolderFileCountThreshold=0',
             '.Set FolderSizeThreshold=0', '.Set MaxCabinetSize=0',
             f'.Set InfFileName="{WORK / "driver.inf"}"', f'.Set RptFileName="{WORK / "driver.rpt"}"']
    lines += [f'"{f}" "{key}"' for f, key in cab]
    ddf.write_text('\r\n'.join(lines) + '\r\n', encoding='mbcs')
    (WORK / CABINET).unlink(missing_ok=True)
    r = subprocess.run(['makecab.exe', '/F', str(ddf)], cwd=WORK, capture_output=True, text=True, errors='replace')
    if r.returncode or not (WORK / CABINET).is_file():
        fail(f'makecab failed:\n{r.stdout}{r.stderr}')

    db = Database(tmp)
    db.set_codepage(CODEPAGE)
    for t in SCHEMA:
        db.create_table(t)

    db.insert('Property', [
        ('ProductCode', product_code), ('ProductName', PRODUCT_NAME), ('ProductVersion', msi_ver),
        ('ProductLanguage', str(LANGUAGE)), ('Manufacturer', MANUFACTURER), ('UpgradeCode', UPGRADE_CODE),
        ('ALLUSERS', '1'), ('ARPNOMODIFY', '1'),
        ('ARPCOMMENTS', f'Драйвер {driver_ver} для Windows 7 SP1, 8.1, 10 и 11 (x86 и x64)'),
        ('SecureCustomProperties', 'NEWERFOUND;PREVIOUSFOUND'),
    ])
    db.insert('Directory', [
        ('TARGETDIR', None, 'SourceDir'),
        ('ProgramFilesFolder', 'TARGETDIR', '.'),
        ('INSTALLDIR', 'ProgramFilesFolder', 'LBP810|LBP-810 Driver'),
        ('DRIVERDIR', 'INSTALLDIR', 'driver'),
    ] + [(f'DRIVER_{a}', 'DRIVERDIR', a) for a in ARCHS])
    db.insert('Component', components)
    db.insert('Feature', [('Driver', None, PRODUCT_NAME, None, 0, 1, 'INSTALLDIR', 16)])  # UIDisallowAbsent
    db.insert('FeatureComponents', [('Driver', c[0]) for c in components])
    db.insert('File', files)
    db.insert('Media', [(1, len(files), None, '#' + CABINET)])
    db.insert('_Streams', [(CABINET, Stream(WORK / CABINET))])

    # Deferred custom actions (in-script, no impersonation = LocalSystem); type 51 actions pass the
    # package folder as their CustomActionData. The x64 DLL runs on 64-bit Windows, the x86 one on
    # 32-bit Windows.
    deferred, rollback, set_property = 0x1 | 0x400 | 0x800, 0x1 | 0x400 | 0x100 | 0x800, 51
    binaries, actions, texts, execute = [], [], [], []
    for arch, sfx in ARCHS.items():
        bits = 'VersionNT64' if sfx == 'x64' else 'NOT VersionNT64'
        binaries.append((f'LBP810CA_{sfx}', Stream(SETUP / arch / 'LBP810CA.DLL')))
        steps = [
            # Before RemoveFiles: the certificate file is still there.
            (1700, f'UninstallDriver_{sfx}', deferred, 'UninstallDriver',
             f'{bits} AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE', 'Удаление драйвера принтера Canon LBP-810'),
            # A failed fresh install takes everything away again; a failed upgrade keeps the driver.
            (6500, f'RollbackDriver_{sfx}', rollback, 'UninstallDriver',
             f'{bits} AND NOT Installed AND NOT PREVIOUSFOUND', 'Отмена установки драйвера принтера'),
            (6504, f'InstallDriver_{sfx}', deferred, 'InstallDriver',
             f'{bits} AND NOT (REMOVE="ALL")', 'Установка драйвера принтера Canon LBP-810'),
        ]
        if sfx == 'x86':
            steps = [(seq + 2, *rest) for seq, *rest in steps]
        for seq, action, typ, entry, cond, text in steps:
            actions += [(f'Set_{action}', set_property, action, '[DRIVERDIR]'), (action, typ, f'LBP810CA_{sfx}', entry)]
            execute += [(f'Set_{action}', cond, seq), (action, cond, seq + 1)]
            texts.append((action, text))
    db.insert('Binary', binaries)
    db.insert('CustomAction', actions)
    db.insert('ActionText', texts)

    db.insert('Upgrade', [
        # Older versions and rebuilds of this one are removed first (major upgrade).
        (UPGRADE_CODE, None, msi_ver, None, 0x200 | 0x1, None, 'PREVIOUSFOUND'),  # VersionMaxInclusive, MigrateFeatures
        (UPGRADE_CODE, msi_ver, None, None, 0x2, None, 'NEWERFOUND'),  # OnlyDetect
    ])
    db.insert('LaunchCondition', [
        ('VersionNT > 601 OR (VersionNT = 601 AND ServicePackLevel >= 1)',
         'Для установки нужна Windows 7 с пакетом обновления 1 (SP1) или более новая версия Windows.'),
        ('NOT NEWERFOUND', 'Уже установлена более новая версия драйвера принтера Canon LBP-810.'),
    ])
    ui = [('FindRelatedProducts', 25), ('LaunchConditions', 100), ('ValidateProductID', 700),
          ('CostInitialize', 800), ('FileCost', 900), ('CostFinalize', 1000), ('MigrateFeatureStates', 1200)]
    db.insert('InstallUISequence', [(a, None, s) for a, s in ui + [('ExecuteAction', 1300)]])
    db.insert('InstallExecuteSequence', [(a, None, s) for a, s in ui + [
        ('InstallValidate', 1400), ('RemoveExistingProducts', 1401), ('InstallInitialize', 1500),
        ('ProcessComponents', 1600), ('UnpublishFeatures', 1800), ('RemoveFiles', 3500), ('InstallFiles', 4000),
        ('RegisterUser', 6000), ('RegisterProduct', 6100), ('PublishFeatures', 6300), ('PublishProduct', 6400),
        ('InstallFinalize', 6600)]] + execute)
    cost = [('CostInitialize', 800), ('FileCost', 900), ('CostFinalize', 1000)]
    db.insert('AdminUISequence', [(a, None, s) for a, s in cost + [('ExecuteAction', 1300)]])
    db.insert('AdminExecuteSequence', [(a, None, s) for a, s in cost + [
        ('InstallValidate', 1400), ('InstallInitialize', 1500), ('InstallAdminPackage', 3900),
        ('InstallFiles', 4000), ('InstallFinalize', 6600)]])

    t = int((time.time() + 11644473600) * 10 ** 7)
    now = wintypes.FILETIME(t & 0xFFFFFFFF, t >> 32)
    db.summary([
        (1, CODEPAGE), (2, 'Installation Database'), (3, PRODUCT_NAME), (4, MANUFACTURER),
        (5, 'Installer, Canon, LBP-810, printer driver'), (6, f'{PRODUCT_NAME} {driver_ver}'),
        (7, f'Intel;{LANGUAGE}'), (9, package_code), (12, now), (13, now),
        (14, 200),  # Windows Installer 2.0 schema
        (15, 2),  # long file names, compressed
        (18, 'relbp810 make-msi.py'), (19, 2),  # read-only recommended
    ])
    db.commit()
    return tmp, product_code


def sign(path):
    kits = sorted(Path(r'C:\Program Files (x86)\Windows Kits\10\bin').glob('10.*/x64/signtool.exe'))
    if not kits:
        print('make-msi: signtool not found, the MSI is unsigned')
        return
    thumb = hashlib.sha1((PKG / 'cnlb810.cer').read_bytes()).hexdigest()  # the package's certificate
    r = subprocess.run([str(kits[-1]), 'sign', '/q', '/fd', 'sha256', '/sha1', thumb, '/d', PRODUCT_NAME, str(path)],
                       capture_output=True, text=True, errors='replace')
    if r.returncode:
        print(f'make-msi: signing failed, the MSI is unsigned\n{r.stdout}{r.stderr}')


def main():
    check_package()
    check_win7([f for a in ARCHS for f in list((PKG / a).glob('*.DLL')) + [SETUP / a / 'LBP810CA.DLL']])
    driver_ver, msi_ver = versions()
    check_version_reuse(driver_ver)
    out = ROOT / 'out' / f'LBP-810-driver-{driver_ver}.msi'
    tmp, product_code = build(out, driver_ver, msi_ver)
    sign(tmp)
    out.unlink(missing_ok=True)
    tmp.replace(out)
    print(f'MSI ready: {out} ({out.stat().st_size // 1024} KB, ProductVersion {msi_ver}, ProductCode {product_code})')


if __name__ == '__main__':
    main()
