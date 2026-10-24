# SPDX-License-Identifier: MIT
"""Configure/build only the neutral CHD decoder; no Play VM or firmware inputs."""
import argparse,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--configure-only',action='store_true');a=p.parse_args()
 source=a.source.resolve();build=a.build.resolve();sdk=a.sdk.resolve()
 if build==Path(build.anchor) or source==build or source in build.parents or ROOT==build or ROOT in build.parents:raise ValueError('Fresh external build directory required')
 lock=json.loads((ROOT/'config/ps2-core.lock.json').read_text())
 revision=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
 if revision!=lock['revision']:raise ValueError('Wrong Play source pin for standalone decoder')
 if subprocess.check_output(['git','-C',str(source),'diff','HEAD','--'],text=True).strip():raise ValueError('Play source must be pristine')
 for dep in lock['dependencies']:
  child=source/dep['path']
  head=subprocess.check_output(['git','-C',str(child),'rev-parse','HEAD'],text=True).strip()
  if head!=dep['revision']:raise ValueError('Wrong dependency pin: '+dep['path'])
  if subprocess.check_output(['git','-C',str(child),'diff','HEAD','--'],text=True).strip():raise ValueError('Dependency source must be pristine')
 cmake=sdk/'cmake'/lock['cmake']/'bin/cmake.exe';ndk=sdk/'ndk'/lock['ndk'];ninja=cmake.with_name('ninja.exe')
 subprocess.run([str(cmake),'-S',str(ROOT/'scripts/ps2/decoder-host'),'-B',str(build),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared','-DCMAKE_BUILD_TYPE=Release','-DPLAY_SOURCE='+source.as_posix()],check=True)
 if not a.configure_only:subprocess.run([str(cmake),'--build',str(build),'--target','ps2_chd_bridge','--parallel','2'],check=True)
 print('DECODER_COMPONENT_ONLY_NO_VM_NO_BIOS')
if __name__=='__main__':main()
