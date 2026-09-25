package dev.mediocre.lab;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Handler;
import android.view.*;
import android.widget.*;
import android.text.InputType;
import android.text.Editable;
import android.text.TextWatcher;
import org.json.*;
import java.io.*;
import java.util.*;

final class LabUI {
    final Activity activity;final boolean ready;final String initError;final Handler handler=new Handler();
    FrameLayout root;LinearLayout bar,flight;ScrollView panel;LinearLayout content;WorldView world;
    TextView status;Button pauseButton;JSONObject state=new JSONObject();boolean disposed,multi,panelOpen;
    String mode="play",panelKind="tools",lastUiSnapshot="";long sequence;Set<Integer> keys=new HashSet<>();
    float mx,my,mz;long lastErrorTime;String lastError="";boolean touchDragging;
    LabUI(Activity a,boolean ok,String error){activity=a;ready=ok;initError=error;}
    int dp(float value){return (int)(value*activity.getResources().getDisplayMetrics().density+.5f);}
    JSONObject json(Object... pairs){JSONObject j=new JSONObject();try{for(int i=0;i<pairs.length;i+=2)j.put((String)pairs[i],pairs[i+1]);}catch(JSONException ignored){}return j;}
    JSONArray vec(double x,double y,double z){return new JSONArray(Arrays.asList(x,y,z));}
    void send(JSONObject j){try{j.put("sequence",++sequence);}catch(JSONException ignored){}LabBridge.send(j);}
    void op(String name){send(json("op",name));}
    void message(String s){Toast.makeText(activity,s,Toast.LENGTH_LONG).show();}
    LinearLayout row(){LinearLayout r=new LinearLayout(activity);r.setOrientation(LinearLayout.HORIZONTAL);r.setGravity(Gravity.CENTER_VERTICAL);return r;}
    LinearLayout column(){LinearLayout r=new LinearLayout(activity);r.setOrientation(LinearLayout.VERTICAL);return r;}
    TextView text(String value){TextView t=new TextView(activity);t.setText(value);t.setTextColor(0xffe7edf6);t.setTextSize(13);t.setPadding(dp(5),dp(4),dp(5),dp(4));return t;}
    Button button(String value,Runnable action){Button b=new Button(activity);b.setText(value);b.setAllCaps(false);b.setTextSize(12);b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(dp(35));b.setMinimumHeight(dp(35));b.setPadding(dp(6),0,dp(6),0);b.setOnClickListener(v->{try{action.run();}catch(Exception e){message(e.getMessage());}});return b;}
    void buttons(LinearLayout parent,Object... items){LinearLayout r=row();for(int i=0;i<items.length;i+=2)r.addView(button((String)items[i],(Runnable)items[i+1]),new LinearLayout.LayoutParams(0,dp(38),1));parent.addView(r);}
    void heading(String s){TextView t=text(s);t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);t.setTextColor(0xff84e3d5);content.addView(t);}
    void note(String s){content.addView(text(s));}
    interface ListEntry {View view(JSONObject value,int index);}
    void pagedList(JSONArray data,String hint,ListEntry entry){
        if(data==null||data.length()==0){note("None captured in this scene.");return;}
        // Bound the number of Android views even for a whole campaign. The
        // native selection and export commands still operate on every object.
        final int pageSize=12;final int[] page={0};final ArrayList<Integer> matches=new ArrayList<>();
        EditText filter=new EditText(activity);filter.setSingleLine(true);filter.setTextSize(13);filter.setTextColor(Color.WHITE);filter.setHintTextColor(0xffaab7c9);filter.setHint(hint);content.addView(filter);
        LinearLayout navigation=row(),entries=column();TextView count=text("");
        final Runnable[] refresh=new Runnable[1];
        Button previous=button("Previous",()->{if(page[0]>0){--page[0];refresh[0].run();}});
        Button next=button("Next",()->{if((page[0]+1)*pageSize<matches.size()){++page[0];refresh[0].run();}});
        navigation.addView(previous,new LinearLayout.LayoutParams(0,dp(38),1));
        navigation.addView(count,new LinearLayout.LayoutParams(0,dp(38),1));
        navigation.addView(next,new LinearLayout.LayoutParams(0,dp(38),1));content.addView(navigation);content.addView(entries);
        refresh[0]=()->{
            entries.removeAllViews();int start=page[0]*pageSize,end=Math.min(start+pageSize,matches.size());
            count.setText(matches.isEmpty()?"0 results":(start+1)+"-"+end+" / "+matches.size());
            previous.setEnabled(page[0]>0);next.setEnabled(end<matches.size());
            for(int i=start;i<end;i++){int index=matches.get(i);entries.addView(entry.view(data.optJSONObject(index),index));}
        };
        Runnable search=()->{
            matches.clear();page[0]=0;String query=filter.getText().toString().trim().toLowerCase(Locale.ROOT);
            for(int i=0;i<data.length();i++){JSONObject value=data.optJSONObject(i);if(value==null)continue;
                String label=value.optString("name")+" "+value.optString("type")+" "+value.optString("key")+" "+value.optString("path")+" "+value.optString("index");
                if(query.isEmpty()||label.toLowerCase(Locale.ROOT).contains(query))matches.add(i);
            }refresh[0].run();
        };
        filter.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int start,int count,int after){}public void onTextChanged(CharSequence s,int start,int before,int count){handler.removeCallbacks(search);handler.postDelayed(search,180);}public void afterTextChanged(Editable e){}});
        search.run();
    }
    void setting(String key,Object value){send(json("op","settings",key,value));}
    interface Toggle {void accept(boolean value);}
    void check(String label,boolean value,Toggle action){CheckBox box=new CheckBox(activity);box.setText(label);box.setTextColor(Color.WHITE);box.setTextSize(13);box.setChecked(value);box.setOnCheckedChangeListener((b,v)->action.accept(v));content.addView(box);}
    void install(){
        root=new FrameLayout(activity);root.setClipChildren(false);
        world=new WorldView();root.addView(world,new FrameLayout.LayoutParams(-1,-1));world.setVisibility(View.GONE);
        bar=row();bar.setPadding(dp(3),0,dp(3),0);bar.setBackgroundColor(0xdd142033);
        bar.addView(button("LAB",()->showPanel("tools")));
        bar.addView(button("Play",()->setMode("play")));
        bar.addView(button("Camera",()->setMode("camera")));
        bar.addView(button("Edit",()->{setMode("edit");showPanel("edit");}));
        pauseButton=button("Pause",()->send(json("op","pause","value",!state.optBoolean("paused"))));bar.addView(pauseButton);
        FrameLayout.LayoutParams bp=new FrameLayout.LayoutParams(-2,dp(40),Gravity.TOP|Gravity.LEFT);root.addView(bar,bp);
        status=text(ready?"Starting native Lab…":"Lab could not start: "+initError);status.setTextSize(11);status.setBackgroundColor(0xbb142033);
        status.setMaxLines(2);FrameLayout.LayoutParams sp=new FrameLayout.LayoutParams(Math.min(dp(400),activity.getResources().getDisplayMetrics().widthPixels),dp(40),Gravity.TOP|Gravity.LEFT);sp.topMargin=dp(40);root.addView(status,sp);
        flight=column();flight.setBackgroundColor(0x99142033);
        buttons(flight,"Forward",(Runnable)()->{},"Up",(Runnable)()->{});
        flight.removeAllViews();
        LinearLayout top=row();top.addView(hold("Forward",0,0,1));top.addView(hold("Up",0,1,0));flight.addView(top);
        LinearLayout mid=row();mid.addView(hold("Left",-1,0,0));mid.addView(hold("Right",1,0,0));flight.addView(mid);
        LinearLayout bottom=row();bottom.addView(hold("Back",0,0,-1));bottom.addView(hold("Down",0,-1,0));flight.addView(bottom);
        flight.addView(button("Look behind",()->op("look_back")));
        FrameLayout.LayoutParams fp=new FrameLayout.LayoutParams(dp(170),-2,Gravity.BOTTOM|Gravity.LEFT);root.addView(flight,fp);flight.setVisibility(View.GONE);
        activity.addContentView(root,new ViewGroup.LayoutParams(-1,-1));handler.post(poll);
    }
    Button hold(String label,float x,float y,float z){Button b=button(label,()->{});b.setLayoutParams(new LinearLayout.LayoutParams(0,dp(42),1));b.setOnTouchListener((v,e)->{
        if(e.getActionMasked()==MotionEvent.ACTION_DOWN){mx=x;my=y;mz=z;move();return true;}
        if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){mx=my=mz=0;move();return true;}return true;});return b;}
    void move(){send(json("op","move","value",vec(mx,my,mz)));}
    void setMode(String value){mx=my=mz=0;keys.clear();send(json("op","mode","value",value));mode=value;hidePanel();updateMode();}
    void updateMode(){boolean input=!mode.equals("play")&&!mode.equals("tools");world.setVisibility(input?View.VISIBLE:View.GONE);flight.setVisibility(input&&!panelOpen?View.VISIBLE:View.GONE);updateInputRegions();}
    void updateInputRegions(){
        if(!ready||bar==null)return;int[] location=new int[2];bar.getLocationInWindow(location);
        LabBridge.inputRegions(new float[]{location[0],location[1],location[0]+bar.getWidth(),location[1]+bar.getHeight()},
            panelOpen||(!mode.equals("play")&&!mode.equals("tools")));
    }
    void hidePanel(){if(panel!=null)root.removeView(panel);panel=null;content=null;panelOpen=false;updateMode();}
    void showPanel(String kind){
        if(!ready){message(initError);return;}hidePanel();panelOpen=true;panelKind=kind;
        try{state=new JSONObject(LabBridge.snapshot());}catch(JSONException e){message("Could not read the scene: "+e.getMessage());}
        if(kind.equals("tools")&&(mode.equals("play")||mode.equals("look")||mode.equals("player"))) {send(json("op","mode","value","tools"));mode="tools";}
        panel=new ScrollView(activity);panel.setBackgroundColor(0xf21a2639);content=column();content.setPadding(dp(6),dp(4),dp(6),dp(8));panel.addView(content);
        int width=Math.min(dp(340),(int)(activity.getResources().getDisplayMetrics().widthPixels*.79f));
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(width,-1,Gravity.RIGHT);p.topMargin=dp(42);root.addView(panel,p);
        buttons(content,kind.equals("edit")?"Edit objects":"Developer tools",(Runnable)()->{},"Close",(Runnable)this::hidePanel);
        if(kind.equals("edit"))editorPanel();else toolsPanel();updateMode();
    }
    void toolsPanel(){
        note("Opening tools pauses the world. Choose Run world to simulate, or Play to return to normal controls.");
        buttons(content,"Pause world",(Runnable)()->send(json("op","pause","value",true)),"Run world",(Runnable)()->send(json("op","pause","value",false)));
        buttons(content,"Free camera",(Runnable)()->setMode("camera"),"Look around + play",(Runnable)()->setMode("look"));
        buttons(content,"Move player",(Runnable)()->setMode("player"),"Original play",(Runnable)()->setMode("play"));
        note("Camera: drag to look, hold arrows to fly. Move player: arrows move the actual character/ball. Camera mode leaves player position unchanged.");
        heading("Movement and time");
        presets("Fly speed", "move_speed",new double[]{1,2,5,10,25,50,100});
        presets("Look sensitivity","look_speed",new double[]{.25,.5,1,2,4});
        presets("World speed","simulation_speed",new double[]{.25,.5,1,2,5});
        JSONObject caps=state.optJSONObject("capabilities");
        check(caps==null?"Immortal":caps.optString("immortality_label","Immortal"),state.optBoolean("immortal"),v->setting("immortal",v));
        check("Noclip when moving player",state.optBoolean("noclip",true),v->setting("noclip",v));
        heading("View");
        check("Custom field of view",state.optBoolean("fov_override"),v->setting("fov_override",v));
        TextView label=text("FOV: "+(int)state.optDouble("fov",70)+"°");content.addView(label);
        SeekBar fov=new SeekBar(activity);fov.setMax(130);fov.setProgress((int)state.optDouble("fov",70)-20);
        fov.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){setting("fov",s.getProgress()+20);}public void onProgressChanged(SeekBar s,int n,boolean u){label.setText("FOV: "+(n+20)+"°");}});content.addView(fov);
        if(state.optBoolean("fog_supported"))check("Show distance fog",state.optBoolean("fog",true),v->setting("fog",v));
        buttons(content,"Save camera spot",(Runnable)()->op("bookmark_save"),"Restore spot",(Runnable)()->{op("bookmark_restore");hidePanel();});
        heading("Teleport");note(state.optString("world_axes"));
        JSONObject player=state.optJSONObject("player"),camera=state.optJSONObject("camera");
        JSONArray initial=player!=null?player.optJSONArray("position"):camera!=null?camera.optJSONArray("position"):null;
        final EditText[] position=vectorInputs("Position",initial);
        buttons(content,"Move camera here",(Runnable)()->{send(json("op","teleport_camera","position",values(position)));hidePanel();},"Move player here",(Runnable)()->send(json("op","teleport_player","position",values(position))));
        heading("Levels and loaded world");
        if("pinout".equals(state.optString("game")))content.addView(button("Start a new run",()->op("start_run")));
        content.addView(button("Return to main menu",()->{op("return_menu");setMode("play");}));
        note("Current section: "+state.optString("current_section","none")+" · "+state.optInt("object_count")+" captured objects");
        buttons(content,"Reload level",(Runnable)()->op("reload_level"),"Refresh list",(Runnable)()->showPanel("tools"));
        JSONArray sections=state.optJSONArray("sections");
        pagedList(sections,"Find section by name or index",(s,i)->{
            final int index=s.optInt("index",i);LinearLayout item=column();
            String flag=s.optBoolean("current")?"▶ ":s.optBoolean("active")?"● ":s.optBoolean("loaded")?"○ ":"· ";
            if(caps!=null&&caps.optBoolean("sections")) {
                Button b=button(flag+index+"  "+s.optString("name"),()->send(json("op","goto_section","index",index)));
                b.setTextColor(s.optBoolean("current")?0xff008577:Color.DKGRAY);item.addView(b);
            } else item.addView(text("Loaded level: "+s.optString("name")));
            if(s.has("bodies"))item.addView(text("    "+s.optInt("bodies")+" bodies · loading stage "+s.optInt("stage")));
            return item;
        });
        JSONArray levels=state.optJSONArray("levels");
        if(levels!=null&&levels.length()>0)pagedList(levels,"Find a campaign level",(level,i)->{
            final String path=level.optString("path");
            return button(level.optString("name",path),()->send(json("op","goto_level","path",path)));
        });
        heading("Research data");
        note("Engine: "+state.optString("engine")+"\nSnapshot and event log use native objects and loaded level data.");
        buttons(content,"Export scene JSON",(Runnable)()->op("export_scene"),"Copy Lab files",(Runnable)this::copyFiles);
        note("Keyboard: F1 tools, WASD move, Q/E down/up, arrows look, Space pause, Escape normal play.");
    }
    void presets(String label,String key,double[] values){content.addView(text(label+": "+state.optDouble(key,1)));LinearLayout r=row();for(double value:values)r.addView(button(String.format(Locale.ROOT,"%s",value==(int)value?""+(int)value:""+value),()->setting(key,value)),new LinearLayout.LayoutParams(0,dp(37),1));content.addView(r);}
    EditText number(double value){EditText e=new EditText(activity);e.setText(String.format(Locale.ROOT,"%.3f",value));e.setTextColor(Color.WHITE);e.setTextSize(12);e.setSelectAllOnFocus(true);e.setSingleLine(true);e.setInputType(InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_FLAG_DECIMAL|InputType.TYPE_NUMBER_FLAG_SIGNED);return e;}
    EditText[] vectorInputs(String label,JSONArray values){content.addView(text(label+" X / Y / Z"));LinearLayout r=row();EditText[] input=new EditText[3];for(int i=0;i<3;i++){input[i]=number(values==null?0:values.optDouble(i));r.addView(input[i],new LinearLayout.LayoutParams(0,dp(44),1));}content.addView(r);return input;}
    JSONArray values(EditText[] input){return vec(Double.parseDouble(input[0].getText().toString()),Double.parseDouble(input[1].getText().toString()),Double.parseDouble(input[2].getText().toString()));}
    void editorPanel(){
        note("World paused. Close this panel, then drag a visible object to move it in the camera plane. Save selection to keep edits for the next run.");
        check("Add objects to selection",multi,v->{multi=v;});
        buttons(content,"Select all loaded",(Runnable)()->{op("select_all");hidePanel();},"Clear selection",(Runnable)()->{op("clear_selection");hidePanel();});
        buttons(content,"Undo",(Runnable)()->op("undo"),"Redo",(Runnable)()->op("redo"));
        buttons(content,"Save selection",(Runnable)()->op("save_edits"),"Inspect selection",(Runnable)()->showPanel("edit"));
        JSONArray selected=state.optJSONArray("selected");int count=selected==null?0:selected.length();heading(count+" selected · "+state.optInt("saved_edit_count")+" saved overrides");
        if(count==1){
            JSONObject o=selected.optJSONObject(0);note(o.optString("name")+"\n"+o.optString("type")+"\n"+o.optString("key")+"\nSection: "+o.optString("section"));
            EditText[] pos=vectorInputs("Position",o.optJSONArray("position"));EditText[] rot=vectorInputs("Rotation (degrees)",o.optJSONArray("rotation_degrees"));EditText[] scale=vectorInputs("Scale",o.optJSONArray("scale"));
            content.addView(button("Apply transform",()->send(json("op","transform","position",values(pos),"rotation",values(rot),"scale",values(scale)))));
            JSONObject details=o.optJSONObject("detail");if(details!=null){java.util.Iterator<String> names=details.keys();while(names.hasNext()){String key=names.next();note(key+": "+details.optString(key));}}
        }
        heading("Move selection together");EditText step=number(1);content.addView(step);
        for(int axis=0;axis<3;axis++){final int a=axis;String name=new String[]{"X","Y","Z"}[axis];buttons(content,name+" −",(Runnable)()->translate(a,-Double.parseDouble(step.getText().toString())),name+" +",(Runnable)()->translate(a,Double.parseDouble(step.getText().toString())));}
        buttons(content,"Rotate Z −15°",(Runnable)()->send(json("op","transform","rotate",vec(0,0,-15))),"Rotate Z +15°",(Runnable)()->send(json("op","transform","rotate",vec(0,0,15))));
        buttons(content,"Scale ×0.9",(Runnable)()->send(json("op","transform","scale_by",vec(.9,.9,.9))),"Scale ×1.1",(Runnable)()->send(json("op","transform","scale_by",vec(1.1,1.1,1.1))));
        heading("Loaded objects");JSONArray objects=state.optJSONArray("objects");
        pagedList(objects,"Find an object by name, type or ID",(o,i)->{final String key=o.optString("key");String name=o.optString("name");return button((name.isEmpty()?o.optString("type"):name)+" · "+key.substring(key.lastIndexOf('/')+1),()->{send(json("op","select","key",key,"add",multi));hidePanel();});});
        heading("Restore original data");note("Clear saved overrides, then Reload level in LAB. Original APK assets are preserved.");content.addView(button("Clear saved overrides",()->op("clear_saved_edits")));
    }
    void translate(int axis,double amount){double[] v={0,0,0};v[axis]=amount;send(json("op","transform","move",vec(v[0],v[1],v[2])));}
    void copyFiles(){try{
        File from=new File(activity.getFilesDir(),"mediocre-lab"),to=new File(activity.getExternalFilesDir(null),"mediocre-lab");to.mkdirs();File[] files=from.listFiles();
        if(files!=null)for(File f:files)if(f.isFile()&&!f.getName().endsWith(".tmp"))try(InputStream in=new FileInputStream(f);OutputStream out=new FileOutputStream(new File(to,f.getName()))){byte[] b=new byte[8192];int n;while((n=in.read(b))>0)out.write(b,0,n);}
        message("Copied to "+to.getAbsolutePath());
    }catch(Exception e){message(e.toString());}}
    final Runnable poll=new Runnable(){public void run(){
        if(disposed)return;try{
            if(ready){String snapshot=LabBridge.snapshotUi();if(!snapshot.equals(lastUiSnapshot)){
                lastUiSnapshot=snapshot;state=new JSONObject(snapshot);String reported=state.optString("mode",mode);if(!reported.equals(mode)){mode=reported;updateMode();}
                boolean paused=state.optBoolean("paused");String pauseLabel=paused?"Run":"Pause";if(!pauseLabel.contentEquals(pauseButton.getText()))pauseButton.setText(pauseLabel);
                JSONObject player=state.optJSONObject("player"),cam=state.optJSONObject("camera");
                String label=mode+" · WORLD "+(paused?"PAUSED":"RUNNING")+" · section "+state.optString("current_section","-")+"\n"+
                    (mode.equals("play")?"Player "+coords(player):"Camera "+coords(cam));
                if(!label.contentEquals(status.getText()))status.setText(label);
                String error=state.optString("error","");if(!error.isEmpty()&&!error.equals(lastError)){message(error);lastError=error;}if(error.isEmpty())lastError="";
                world.invalidate();}updateInputRegions();
            }
        }catch(Exception e){status.setText("Lab: "+e.getMessage());}handler.postDelayed(this,300);
    }};
    String coords(JSONObject o){JSONArray p=o==null?null:o.optJSONArray("position");return p==null?"-":String.format(Locale.ROOT,"%.2f, %.2f, %.2f",p.optDouble(0),p.optDouble(1),p.optDouble(2));}
    boolean handleKey(KeyEvent event){
        int k=event.getKeyCode();boolean down=event.getAction()==KeyEvent.ACTION_DOWN;
        if(k==KeyEvent.KEYCODE_F1){if(down&&event.getRepeatCount()==0)showPanel("tools");return true;}
        if(mode.equals("play"))return false;
        if(k==KeyEvent.KEYCODE_ESCAPE||k==KeyEvent.KEYCODE_BACK){if(down){if(panelOpen)hidePanel();else setMode("play");}return true;}
        if(panelOpen)return false;
        if(k==KeyEvent.KEYCODE_SPACE){if(down&&event.getRepeatCount()==0)send(json("op","pause","value",!state.optBoolean("paused")));return true;}
        if(k==KeyEvent.KEYCODE_DPAD_LEFT||k==KeyEvent.KEYCODE_DPAD_RIGHT||k==KeyEvent.KEYCODE_DPAD_UP||k==KeyEvent.KEYCODE_DPAD_DOWN){if(down)send(json("op","look","x",k==KeyEvent.KEYCODE_DPAD_LEFT?-.06:k==KeyEvent.KEYCODE_DPAD_RIGHT?.06:0,"y",k==KeyEvent.KEYCODE_DPAD_UP?-.06:k==KeyEvent.KEYCODE_DPAD_DOWN?.06:0));return true;}
        if(!Arrays.asList(KeyEvent.KEYCODE_W,KeyEvent.KEYCODE_A,KeyEvent.KEYCODE_S,KeyEvent.KEYCODE_D,KeyEvent.KEYCODE_Q,KeyEvent.KEYCODE_E).contains(k))return false;
        if(down)keys.add(k);else keys.remove(k);mx=(keys.contains(KeyEvent.KEYCODE_D)?1:0)-(keys.contains(KeyEvent.KEYCODE_A)?1:0);my=(keys.contains(KeyEvent.KEYCODE_E)?1:0)-(keys.contains(KeyEvent.KEYCODE_Q)?1:0);mz=(keys.contains(KeyEvent.KEYCODE_W)?1:0)-(keys.contains(KeyEvent.KEYCODE_S)?1:0);move();return true;
    }
    void dispose(){disposed=true;handler.removeCallbacks(poll);if(root!=null&&root.getParent() instanceof ViewGroup)((ViewGroup)root.getParent()).removeView(root);}
    final class WorldView extends View {
        Paint paint=new Paint(3);float x,y;boolean dragging;
        WorldView(){super(activity);setFocusable(false);}
        protected void onDraw(Canvas canvas){
            JSONArray selected=state.optJSONArray("selected");if(selected==null)return;paint.setColor(0xff4effc2);paint.setStrokeWidth(dp(2));paint.setStyle(Paint.Style.STROKE);
            for(int n=0;n<selected.length();n++){JSONObject o=selected.optJSONObject(n);JSONArray points=o==null?null:o.optJSONArray("screen_bounds");if(points==null||points.length()!=8)continue;
                for(int i=0;i<8;i++)for(int bit=1;bit<=4;bit*=2)if((i&bit)==0){JSONArray a=points.optJSONArray(i),b=points.optJSONArray(i|bit);if(a!=null&&b!=null)canvas.drawLine((float)a.optDouble(0)*getWidth(),(float)a.optDouble(1)*getHeight(),(float)b.optDouble(0)*getWidth(),(float)b.optDouble(1)*getHeight(),paint);}}
        }
        public boolean onTouchEvent(MotionEvent e){
            if(mode.equals("play")||mode.equals("tools"))return false;float nx=e.getX()/getWidth(),ny=e.getY()/getHeight();
            if(e.getActionMasked()==MotionEvent.ACTION_DOWN){x=e.getX();y=e.getY();dragging=true;if(mode.equals("edit"))send(json("op","drag_begin","x",nx,"y",ny,"add",multi));return true;}
            if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&dragging){if(mode.equals("edit"))send(json("op","drag","x",nx,"y",ny));else send(json("op","look","x",(e.getX()-x)/getWidth()*3.14,"y",(e.getY()-y)/getHeight()*3.14));x=e.getX();y=e.getY();return true;}
            if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){dragging=false;if(mode.equals("edit"))op("drag_end");return true;}return true;
        }
    }
}
