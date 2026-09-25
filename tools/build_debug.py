#!/usr/bin/env python3
"""Build tool paths for the isolated developer input driver."""
from pathlib import Path
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'tools/sdk'
BT=SDK/'build-tools/android-15'
JDK=next((ROOT/'tools/jdk').glob('jdk-*'), ROOT/'tools/jdk/not-installed')
NDK=SDK/'ndk/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64'
ENV=dict(os.environ,JAVA_HOME=str(JDK),ANDROID_SDK_ROOT=str(SDK))
ENV['PATH']=str(JDK/'bin')+':'+ENV.get('PATH','')

def run(*args):
    print('+',' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=ROOT,env=ENV,check=True)
