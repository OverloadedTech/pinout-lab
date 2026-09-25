#!/usr/bin/env python3
"""Build separate PinOut/Granny Smith Lab packages from exact, locally supplied APKs."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,zipfile,xml.etree.ElementTree as ET
import setup_hook
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'tools/sdk';BT=SDK/'build-tools/android-15';JDK=next((ROOT/'tools/jdk').glob('jdk-*'), ROOT/'tools/jdk/not-installed')
NDK=SDK/'ndk/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64'
CONFIG={
 'pinout':{'sha':'81c0f9048c2c12731fefcf0373f0fccb28a2e0626288bc826ba66f5002f37ab7','title':'PinOut Lab',
 'package':'com.mediocre.pinout','library':'pinout','main':'smali_classes2/com/mediocre/pinout/MainActivity.smali',
 'super':'Lcom/google/androidgamesdk/GameActivity;','dex':3,'abis':['arm64-v8a','x86_64'],
 'hashes':{'x86_64':'621979fa69ce22aa2e0b2d919d20c4a4fe918a003fed502bf2a00292d5000c99','arm64-v8a':'ca798b0ffb35d5db7445887db7714ffe23512bd5c6352d6d0b792d91dc13e0c8'}},
 'granny-smith':{'sha':'cb5eb4d70bab1b2c9da9210b18dfecbde09314c2efd0fa3288fcbacd55efda03','title':'Granny Smith Lab',
 'package':'com.mediocre.grannysmith','library':'grannysmith','main':'smali/com/mediocre/grannysmith/Main.smali',
 'super':'Landroid/app/NativeActivity;','dex':2,'abis':['arm64-v8a','armeabi-v7a'],
 'hashes':{'arm64-v8a':'8826828fe8fa80e8da583338bcf5b1b46ebdfe7140c4c001a60093c67199ae52','armeabi-v7a':'ab1fd12d06952937aa802c3a4e6a4e2c413a20ebfa51a95ca33ca5cd75f788f1'}}}
ENV=dict(os.environ,JAVA_HOME=str(JDK));ENV['PATH']=str(JDK/'bin')+':'+ENV.get('PATH','')
def run(*args):
    print('+',' '.join(map(str,args)),flush=True);subprocess.run(list(map(str,args)),cwd=ROOT,env=ENV,check=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def build(game,abis):
    if not (ROOT/'labs'/game/'native').is_dir():
        raise SystemExit('This checkout does not include the '+game+' adapter; use its standalone repository.')
    if not (JDK/'bin/java').is_file():
        raise SystemExit('Missing local JDK. Run python tools/bootstrap.py --components build, then source tools/env.sh.')
    original=ROOT/'incoming'/game/(game+'.apk')
    if not original.is_file():
        raise SystemExit('Supply your compatible APK at '+str(original.relative_to(ROOT)))
    cfg=CONFIG[game];work=ROOT/'build/game-lab'/game;work.mkdir(parents=True,exist_ok=True)
    original=ROOT/'incoming'/game/(game+'.apk')
    if sha(original)!=cfg['sha']:raise RuntimeError('Unsupported input APK hash')
    source=ROOT/'analysis/games'/game/'decoded'
    if not source.exists():run(JDK/'bin/java','-jar',ROOT/'tools/apktool.jar','d',original,'-o',source)
    target=work/'decoded'
    if not target.exists():shutil.copytree(source,target,ignore=shutil.ignore_patterns('build','dist'))
    # The addon uses Android's API 21 ELF loader interfaces. The original Granny
    # manifest advertises API 9, which must not promise support for this addon.
    metadata=(source/'apktool.yml').read_text()
    minimum=re.search(r'(?m)^  minSdkVersion: [\'"]?(\d+)',metadata)
    if not minimum:raise RuntimeError('APK minimum SDK declaration not found')
    metadata=re.sub(r'(?m)^  minSdkVersion: .*$', '  minSdkVersion: '+str(max(21,int(minimum.group(1)))),metadata)
    (target/'apktool.yml').write_text(metadata)
    ns='{http://schemas.android.com/apk/res/android}';ET.register_namespace('android',ns[1:-1])
    manifest=ET.parse(source/'AndroidManifest.xml');root=manifest.getroot();old=cfg['package'];root.set('package',old+'.dev')
    app=root.find('application');app.set(ns+'label',cfg['title']);app.set(ns+'debuggable','true');app.set(ns+'extractNativeLibs','true')
    for node in root.iter():
        for k,v in list(node.attrib.items()):
            if k==ns+'authorities' or (k==ns+'name' and v==old+'.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION'):node.set(k,v.replace(old,old+'.dev'))
        if node.tag=='activity' and node.find('intent-filter/category[@'+ns+'name="android.intent.category.LAUNCHER"]') is not None:
            node.set(ns+'label',cfg['title'])
    manifest.write(target/'AndroidManifest.xml',encoding='utf-8',xml_declaration=True)
    text=(source/cfg['main']).read_text();marker='    invoke-super {p0, p1}, '+cfg['super']+'->onCreate(Landroid/os/Bundle;)V'
    if text.count(marker)!=1:raise RuntimeError('Activity integration anchor differs')
    text=text.replace(marker,'    invoke-static {p0}, Ldev/mediocre/lab/LabBridge;->prepare(Landroid/app/Activity;)V\n\n'+marker+'\n\n    invoke-static {p0}, Ldev/mediocre/lab/LabBridge;->install(Landroid/app/Activity;)V')
    if '.method public dispatchKeyEvent(' in text:raise RuntimeError('Existing key dispatcher needs explicit integration')
    text+='''
.method public dispatchKeyEvent(Landroid/view/KeyEvent;)Z
    .locals 1
    invoke-static {p1}, Ldev/mediocre/lab/LabBridge;->handleKey(Landroid/view/KeyEvent;)Z
    move-result v0
    if-eqz v0, :original_dispatch
    const/4 v0, 0x1
    return v0
    :original_dispatch
    invoke-super {p0, p1}, SUPER->dispatchKeyEvent(Landroid/view/KeyEvent;)Z
    move-result v0
    return v0
.end method
'''.replace('SUPER',cfg['super'])
    (target/cfg['main']).write_text(text)
    classes=work/'classes';shutil.rmtree(classes,ignore_errors=True);classes.mkdir()
    dex=work/'dex';shutil.rmtree(dex,ignore_errors=True);dex.mkdir()
    run(JDK/'bin/javac','--release','8','-classpath',SDK/'platforms/android-35/android.jar','-d',classes,*sorted((ROOT/'labs/common/java').rglob('*.java')))
    run(JDK/'bin/jar','cf',work/'addon.jar','-C',classes,'.')
    run(BT/'d8','--min-api','21','--lib',SDK/'platforms/android-35/android.jar','--output',dex,work/'addon.jar')
    libraries={}
    sources=sorted((ROOT/'labs/common/native').glob('*.cpp'))+sorted((ROOT/'labs'/game/'native').glob('*.cpp'))
    fingerprints={p.relative_to(ROOT).as_posix():sha(p) for p in sources+list((ROOT/'labs/common/native').glob('*.hpp'))+list((ROOT/'labs/common/java').rglob('*.java'))+[Path(__file__),ROOT/'tools/setup_hook.py',ROOT/'dev/native/math.hpp',ROOT/'dev/vendor/json.hpp']}
    source_id=hashlib.sha256(json.dumps(fingerprints,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    for abi in abis:
        if abi not in cfg['abis']:raise RuntimeError('Unsupported game ABI')
        hook=setup_hook.build(abi);dest=work/'native'/abi;dest.mkdir(parents=True,exist_ok=True)
        triple={'x86_64':'x86_64-linux-android21','arm64-v8a':'aarch64-linux-android21','armeabi-v7a':'armv7a-linux-androideabi21'}[abi]
        library=source/'lib'/abi/('lib'+cfg['library']+'.so')
        if sha(library)!=cfg['hashes'][abi]:raise RuntimeError('Unexpected decoded native library')
        from elftools.elf.elffile import ELFFile
        with library.open('rb') as stream:
            symbols={symbol.name for symbol in ELFFile(stream).get_section_by_name('.dynsym').iter_symbols()}
        requested=set(re.findall(r'"(_Z\w+|tdSolverInsertBody)"','\n'.join(p.read_text() for p in sources)))
        if requested-symbols:raise RuntimeError('Native symbols missing: '+', '.join(sorted(requested-symbols)))
        run(NDK/'bin'/(triple+'-clang++'),'-std=c++20','-shared','-fPIC','-fvisibility=hidden','-O2','-g','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare',
            '-static-libstdc++','-Wl,--exclude-libs,ALL','-Wl,-z,max-page-size=16384','-DLAB_LIBRARY_SHA="'+cfg['hashes'][abi]+'"','-DLAB_ADDON_SOURCE_SHA="'+source_id+'"',
            '-I'+str(ROOT/'labs/common/native'),'-I'+str(ROOT/'dev/vendor'),'-I'+str(ROOT/'tools/dobby-stable/include'),
            *sources,hook,'-o',dest/'libmlab.so','-llog','-landroid','-ldl','-lGLESv2')
        shutil.copy2(dest/'libmlab.so',dest/'libmlab.symbols.so');run(NDK/'bin/llvm-strip','--strip-debug',dest/'libmlab.so');libraries[abi]=dest/'libmlab.so'
    run(JDK/'bin/java','-jar',ROOT/'tools/apktool.jar','b',target,'-o',work/'base.apk')
    with zipfile.ZipFile(work/'base.apk') as src,zipfile.ZipFile(work/'unsigned.apk','w') as dst:
        for entry in src.infolist():
            if entry.filename.startswith('META-INF/') and entry.filename.endswith(('.SF','.RSA','.DSA','.MF')):continue
            if entry.filename.startswith('lib/') and entry.filename.split('/')[1] not in abis:continue
            dst.writestr(entry,src.read(entry.filename))
        dst.write(dex/'classes.dex','classes'+str(cfg['dex'])+'.dex',compress_type=zipfile.ZIP_DEFLATED)
        for abi,path in libraries.items():dst.write(path,'lib/'+abi+'/libmlab.so',compress_type=zipfile.ZIP_STORED)
        dst.writestr('assets/lab/config.json',json.dumps({'game':game,'library':cfg['library'],'title':cfg['title'],'hashes':cfg['hashes']}))
        if game=='granny-smith':
            campaign=ET.parse(source/'assets/game.xml.mp3').getroot()
            catalogue=[dict(level.attrib,world=world.attrib['name']) for world in campaign for level in world]
            dst.writestr('assets/lab/catalogue.json',json.dumps(catalogue))
        dst.write(ROOT/'tools/dobby-stable/LICENSE','assets/lab/Dobby-LICENSE.txt',compress_type=zipfile.ZIP_DEFLATED)
        for folder in [ROOT/'labs/common/assets',ROOT/'labs'/game/'assets']:
            if folder.exists():
                for path in sorted(folder.rglob('*')):
                    if path.is_file():dst.write(path,'assets/'+path.relative_to(folder).as_posix(),compress_type=zipfile.ZIP_DEFLATED)
    run(BT/'zipalign','-f','-P','16','4',work/'unsigned.apk',work/'aligned.apk')
    key=ROOT/'build/mediocre-lab.keystore'
    if not key.exists():run(JDK/'bin/keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','mlab','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=Mediocre Local Research Lab')
    result=ROOT/'artifacts'/(game+'-lab.apk');result.parent.mkdir(exist_ok=True)
    run(BT/'apksigner','sign','--ks',key,'--ks-key-alias','mlab','--ks-pass','pass:android','--key-pass','pass:android','--out',result,work/'aligned.apk')
    run(BT/'apksigner','verify','--verbose',result)
    checked=0
    with zipfile.ZipFile(original) as sourceZip,zipfile.ZipFile(result) as built:
        for name in sourceZip.namelist():
            retained=name.startswith('assets/') or (name.startswith('lib/') and name.split('/')[1] in abis)
            if retained:
                if sourceZip.read(name)!=built.read(name):raise RuntimeError('Original game data changed: '+name)
                checked+=1
    for name,expected in fingerprints.items():
        if sha(ROOT/name)!=expected:raise RuntimeError('Source changed during the build: '+name)
    report={'apk':result.relative_to(ROOT).as_posix(),'sha256':sha(result),'input_sha256':cfg['sha'],'package':old+'.dev','abis':abis,
      'unchanged_original_asset_and_library_entries':checked,'source_sha256':fingerprints,'addon_source_sha256':source_id,'dobby_commit':setup_hook.COMMIT}
    (ROOT/'artifacts'/(game+'-build-report.json')).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',choices=list(CONFIG));p.add_argument('--abi',action='append');a=p.parse_args();build(a.game,a.abi or CONFIG[a.game]['abis'])
