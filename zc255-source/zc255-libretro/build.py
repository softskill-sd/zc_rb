"""Offline Windows x64 build; run from a Visual Studio Developer PowerShell."""
from pathlib import Path
import argparse,hashlib,os,shutil,subprocess,sys
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--configure-only',action='store_true');p.add_argument('--skip-configure',action='store_true');p.add_argument('--jobs',type=int,default=4);p.add_argument('--build-dir',type=Path);a=p.parse_args()
if os.name!='nt':raise SystemExit('The release build currently supports Windows x64 only.')
program_files=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))
vs=os.environ.get('VSINSTALLDIR')
if not vs:
    finder=program_files/'Microsoft Visual Studio/Installer/vswhere.exe'
    if finder.is_file():
        vs=subprocess.check_output([str(finder),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
if not vs:raise SystemExit('Install Visual Studio 2022 C++ Build Tools with CMake, Ninja and a Windows SDK.')
vs=Path(vs)
versions=sorted((vs/'VC/Tools/MSVC').glob('*'),reverse=True)
vc=next((v for v in versions if (v/'bin/Hostx64/x64/cl.exe').is_file()),None)
sdk=program_files/'Windows Kits/10'
sdk_versions=sorted((sdk/'Include').glob('*'),reverse=True)
sdk_version=next((v.name for v in sdk_versions if (v/'ucrt').is_dir() and (sdk/'Lib'/v.name/'um/x64').is_dir()),None)
if not vc or not sdk_version:raise SystemExit('A complete x64 MSVC toolchain and Windows SDK are required.')
os.environ['PATH']=';'.join(map(str,[vc/'bin/Hostx64/x64',sdk/'bin'/sdk_version/'x64']))+';'+os.environ.get('PATH','')
os.environ['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'Include'/sdk_version/'ucrt',sdk/'Include'/sdk_version/'shared',sdk/'Include'/sdk_version/'um',sdk/'Include'/sdk_version/'winrt']))
os.environ['LIB']=';'.join(map(str,[vc/'lib/x64',sdk/'Lib'/sdk_version/'ucrt/x64',sdk/'Lib'/sdk_version/'um/x64']))
os.environ['VSINSTALLDIR']=str(vs)
def tool(name,vs_relative=None):
    found=shutil.which(name)
    if not found and vs_relative and os.environ.get('VSINSTALLDIR'):
        candidate=Path(os.environ['VSINSTALLDIR'])/vs_relative
        if candidate.is_file():found=str(candidate)
    if not found:raise SystemExit('Missing '+name+'. Use a Visual Studio 2022 x64 Developer PowerShell with CMake/Ninja installed.')
    return found
cmake=tool('cmake','Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
ninja=tool('ninja','Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
compiler=tool('cl');linker=tool('link');build=a.build_dir.resolve() if a.build_dir else root/'build';dist=root/'dist'
flags='/DWIN32 /D_WINDOWS /experimental:deterministic /wd5048 /pathmap:'+str(root)+'=source /pathmap:'+root.as_posix()+'=source'
args=[cmake,'-S',str(root/'engine'),'-B',str(build),'-G','Ninja Multi-Config','-DCMAKE_MAKE_PROGRAM='+ninja,'-DPYTHON='+sys.executable,
      '-DCMAKE_C_FLAGS='+flags,'-DCMAKE_CXX_FLAGS='+flags+' /GR /EHsc','-DCMAKE_SHARED_LINKER_FLAGS=/Brepro /PDBALTPATH:zc255_libretro.pdb',
      '-DZC255_PHASE_ROOT='+str(root/'engine'),'-DZC255_WORKSPACE_ROOT='+str(root/'engine'),'-DZC255_HOST_SOURCE_ROOT='+str(root/'engine/src'),
      '-DZC255_RUNTIME_ROOT='+str(dist),'-DZC255_DEPENDENCY_PROFILE=allegro-static','-DZC255_LIFECYCLE_CANDIDATE=ON',
      '-DZC255_SPRITE_SELFTEST=ON','-DZC255_EMBEDDED_ASSETS=ON','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL',
      '-DFETCHCONTENT_FULLY_DISCONNECTED=ON','-DWANT_GIT_HOOKS=OFF','-DWANT_ZUPDATER=OFF','-DWANT_SENTRY=OFF',
      '-DWANT_EXAMPLES=OFF','-DWANT_DEMOS=OFF','-DWANT_TESTS=OFF','-DSDL_TEST=OFF','-DSDL_TESTS=OFF','-DJSON_BuildTests=OFF',
      '-DBUILD_TESTING=OFF','-DNFD_BUILD_TESTS=OFF','-DGME_BUILD_DEMO=OFF']
for folder in sorted((root/'dependencies').iterdir()):
    if folder.is_dir():args.append('-DFETCHCONTENT_SOURCE_DIR_'+folder.name.removesuffix('-src').upper()+'='+str(folder))
if a.skip_configure:
    assert (build/'CMakeCache.txt').is_file() and (build/'build.ninja').is_file(),'Configure this build directory first.'
else:
    subprocess.run(args,check=True)
if a.configure_only:sys.exit(0)
def stamp():
    h=hashlib.sha256(b'zc255-public-source-build-v1\0')
    for parent in (root/'engine',root/'dependencies',root/'cmake'):
        for file in sorted(parent.rglob('*')):
            if not file.is_file() or file.name=='snapshot_build.h':continue
            h.update(file.relative_to(root).as_posix().encode());h.update(file.read_bytes())
    for file in [Path(compiler),Path(linker),build/'CMakeCache.txt']:
        h.update(file.name.encode());h.update(file.read_bytes())
    for file in sorted(build.rglob('*.h')):h.update(file.read_bytes())
    digest=h.digest();text='#pragma once\ninline constexpr unsigned char ZC255_SNAPSHOT_BUILD[] = {'+','.join(map(str,digest))+'};\n'
    for file in (root/'engine/src/zc/snapshot_build.h',root/'engine/src/snapshot_build.h'):
        if not file.exists() or file.read_text()!=text:file.write_text(text)
    return digest.hex()
before=stamp();command=[cmake,'--build',str(build),'--config','Release','--target','zplayer','--parallel',str(a.jobs)]
subprocess.run(command,check=True)
after=stamp()
if before!=after:
    subprocess.run(command,check=True)
    assert stamp()==after,'Generated headers changed again; rerun the build.'
print('Core:',dist/'zc255_libretro.dll')
