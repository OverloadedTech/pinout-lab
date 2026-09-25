package dev.mediocre.lab;

import android.app.Activity;
import android.content.res.AssetManager;
import android.os.Build;
import android.view.KeyEvent;
import android.widget.Toast;
import org.json.JSONObject;
import java.io.*;
import java.security.MessageDigest;
import java.util.Locale;

public final class LabBridge {
    private static boolean prepared;
    private static String error="";
    static JSONObject config=new JSONObject();
    static LabUI ui;
    static native String init(AssetManager assets,String files,String hash);
    static native void command(String json);
    static native String snapshot();
    static native String snapshotUi();
    static native void inputRegions(float[] rectangles,boolean exclusive);
    public static synchronized void prepare(Activity activity) {
        activity.getWindow().requestFeature(android.view.Window.FEATURE_NO_TITLE);
        activity.getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN|android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(prepared)return;
        try {
            config=new JSONObject(read(activity.getAssets().open("lab/config.json")));
            String library=config.getString("library");
            File path=new File(activity.getApplicationInfo().nativeLibraryDir,"lib"+library+".so");
            MessageDigest digest=MessageDigest.getInstance("SHA-256");
            try(InputStream stream=new FileInputStream(path)) {
                byte[] buffer=new byte[65536];int count;
                while((count=stream.read(buffer))>0)digest.update(buffer,0,count);
            }
            StringBuilder hex=new StringBuilder();for(byte b:digest.digest())hex.append(String.format(Locale.ROOT,"%02x",b&255));
            String hash=hex.toString();boolean supported=false;JSONObject hashes=config.getJSONObject("hashes");
            java.util.Iterator<String> keys=hashes.keys();while(keys.hasNext())if(hashes.getString(keys.next()).equals(hash))supported=true;
            if(!supported)throw new IllegalStateException("Native game version is not supported by this devkit");
            System.loadLibrary(library);System.loadLibrary("mlab");
            error=init(activity.getAssets(),activity.getFilesDir().getAbsolutePath(),hash);
            prepared=error.isEmpty();
        } catch(Throwable problem) {error=problem.toString();android.util.Log.e("MEDIOCRE_LAB",error,problem);}
    }
    public static void install(final Activity activity) {
        if(activity instanceof android.app.NativeActivity) {
            // NativeActivity takes the entire window surface. Give the original
            // renderer its own SurfaceView so Android can composite real tools
            // above it. Preserve the activity's native input queue and callbacks.
            activity.getWindow().takeSurface(null);
            android.view.SurfaceView surface=new android.view.SurfaceView(activity);
            surface.getHolder().addCallback((android.app.NativeActivity)activity);
            surface.getHolder().setFormat(android.graphics.PixelFormat.RGB_565);
            android.view.ViewGroup content=activity.findViewById(android.R.id.content);
            content.addView(surface,0,new android.view.ViewGroup.LayoutParams(-1,-1));
        }
        activity.getWindow().getDecorView().post(new Runnable(){public void run(){
            if(activity.getActionBar()!=null)activity.getActionBar().hide();
            activity.getWindow().getDecorView().setSystemUiVisibility(5894);
            if(ui!=null)ui.dispose();
            ui=new LabUI(activity,prepared,error);ui.install();
        }});
    }
    public static boolean handleKey(KeyEvent event) {return ui!=null&&ui.handleKey(event);}
    static void send(JSONObject value) {if(prepared)command(value.toString());}
    static String read(InputStream input)throws IOException {
        try(InputStream stream=input;ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] data=new byte[8192];int n;while((n=stream.read(data))>0)out.write(data,0,n);return out.toString("UTF-8");
        }
    }
}
