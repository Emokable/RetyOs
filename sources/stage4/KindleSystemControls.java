import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
import android.graphics.ColorFilter;
import android.graphics.drawable.StateListDrawable;
import android.graphics.drawable.ColorDrawable;
import android.net.*;
import android.os.*;
import android.provider.Settings;
import android.util.Log;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.List;

/** One small system-UID process: five controls and a real TAP NetworkAgent. */
public final class KindleSystemControls {
    static final String TAG = "KindleControls";
    static final String TMP = "/data/local/tmp/";
    static Context context;
    static ImageView modeButton;
    static View navigationBar;
    static TextView deviceStatus;
    static android.app.Dialog settingsDialog;
    static android.graphics.Typeface menuTypeface;
    static boolean animations = true;
    static int animationSteps = 12;
    static android.net.NetworkAgent network;
    static Handler main;
    static int mode;
    static int desktopUid = -1;
    static final class ControlIcon extends Drawable {
        final String kind;
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        ControlIcon(String kind){this.kind=kind;paint.setColor(Color.BLACK);paint.setStrokeWidth(1.8f);paint.setStyle(Paint.Style.STROKE);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);}
        public int getIntrinsicWidth(){return dp(21);}
        public int getIntrinsicHeight(){return dp(21);}
        public int getOpacity(){return PixelFormat.TRANSLUCENT;}
        public void setAlpha(int a){paint.setAlpha(a);}
        public void setColorFilter(ColorFilter f){paint.setColorFilter(f);}
        public void draw(Canvas canvas){
            int save=canvas.save();canvas.translate(getBounds().left,getBounds().top);
            canvas.scale(getBounds().width()/24f,getBounds().height()/24f);
            Path path=new Path();
            if(kind.equals("关闭")){canvas.drawLine(6,6,18,18,paint);canvas.drawLine(18,6,6,18,paint);}
            else if(kind.equals("返回")){path.moveTo(15,4);path.lineTo(7,12);path.lineTo(15,20);canvas.drawPath(path,paint);}
            else if(kind.equals("桌面")){path.moveTo(3,11);path.lineTo(12,3);path.lineTo(21,11);canvas.drawPath(path,paint);
                path.reset();path.moveTo(6,10);path.lineTo(6,21);path.lineTo(10,21);path.lineTo(10,15);path.lineTo(14,15);path.lineTo(14,21);path.lineTo(18,21);path.lineTo(18,10);canvas.drawPath(path,paint);}
            else if(kind.equals("全刷")){canvas.drawArc(new RectF(4,4,20,20),-45,290,false,paint);path.moveTo(20,3);path.lineTo(20,9);path.lineTo(14,9);canvas.drawPath(path,paint);}
            else if(kind.equals("退出")){path.moveTo(12,4);path.lineTo(4,4);path.lineTo(4,20);path.lineTo(12,20);canvas.drawPath(path,paint);
                canvas.drawLine(10,12,22,12,paint);path.reset();path.moveTo(18,8);path.lineTo(22,12);path.lineTo(18,16);canvas.drawPath(path,paint);}
            else {for(int i=0;i<3;i++){float y=6+i*6;canvas.drawLine(4,y,20,y,paint);}canvas.drawCircle(8,6,2,paint);canvas.drawCircle(16,12,2,paint);canvas.drawCircle(10,18,2,paint);}
            canvas.restoreToCount(save);
        }
    }
    static int dp(float value) { return Math.round(value * context.getResources().getDisplayMetrics().density); }
    static void write(String name, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(TMP + name)) {
            out.write(text.getBytes("UTF-8"));
        }
    }
    static Object call(Object target, String name, Class<?>[] types, Object... args) throws Exception {
        return target.getClass().getMethod(name, types).invoke(target, args);
    }
    static void key(int code) {
        if(code==KeyEvent.KEYCODE_BACK && dismissSettings()) return;
        if(code==KeyEvent.KEYCODE_HOME) dismissSettings();
        try {
            Class<?> manager = Class.forName("android.hardware.input.InputManager");
            Object input = manager.getMethod("getInstance").invoke(null);
            Method inject = manager.getMethod("injectInputEvent", InputEvent.class, int.class);
            long now = SystemClock.uptimeMillis();
            inject.invoke(input, new KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0), 0);
            inject.invoke(input, new KeyEvent(now, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP, code, 0), 0);
            Log.i(TAG, "CONTROL_KEY=" + code);
        } catch (Exception error) { Log.e(TAG, "CONTROL_KEY_FAILED", error); }
    }
    static void command(final String request) {
        try {
            if(request.equals("back"))key(KeyEvent.KEYCODE_BACK);
            else if(request.equals("home"))key(KeyEvent.KEYCODE_HOME);
            else if(request.equals("full"))fullRefresh();
            else if(request.equals("mode"))setMode((mode+1)%3);
            else if(request.equals("settings"))showDisplayMenu();
            else if(request.equals("network"))probeNetwork();
            else if(request.equals("animation")){animations=!animations;readerSettings();}
            else if(request.equals("hidebar"))setBarVisible(false);
            else if(request.equals("showbar"))setBarVisible(false);
            else if(request.equals("expand"))setBarVisible(true);
            else if(request.equals("exit"))write("kindle-android-exit","exit\n");
            else if(request.equals("ime-test"))startExternalActivity(new Intent().setClassName("org.kindlelab.apkprobe","org.kindlelab.apkprobe.InputTestActivity"));
            else if(request.equals("files"))startExternalActivity(new Intent().setClassName("org.kindlelab.apkprobe","org.kindlelab.apkprobe.FilesActivity"));
            else if(request.startsWith("launch ")) {
                String pkg=request.substring(7);
                Intent intent=context.getPackageManager().getLaunchIntentForPackage(pkg);
                if(intent==null&&pkg.equals("com.tencent.weread.eink"))intent=new Intent(Intent.ACTION_MAIN).setClassName(pkg,"com.tencent.weread.LauncherActivity");
                if(intent!=null)startExternalActivity(intent);
            } else if(request.startsWith("tap ")) {
                String[] fields=request.split(" ");float x=Float.parseFloat(fields[1]),y=Float.parseFloat(fields[2]);
                if(x<0||y<0||x>=1272||y>=1696)throw new IllegalArgumentException("coordinates");
                Class<?> manager=Class.forName("android.hardware.input.InputManager");
                Object input=manager.getMethod("getInstance").invoke(null);
                Method inject=manager.getMethod("injectInputEvent",InputEvent.class,int.class);
                long now=SystemClock.uptimeMillis();
                MotionEvent down=MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,x,y,0);
                down.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);inject.invoke(input,down,0);down.recycle();
                MotionEvent up=MotionEvent.obtain(now,SystemClock.uptimeMillis(),MotionEvent.ACTION_UP,x,y,0);
                up.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);inject.invoke(input,up,0);up.recycle();
            } else if(request.startsWith("swipe ")) {
                final String[] fields=request.split(" ");
                final float x1=Float.parseFloat(fields[1]),y1=Float.parseFloat(fields[2]),x2=Float.parseFloat(fields[3]),y2=Float.parseFloat(fields[4]);
                final int duration=Integer.parseInt(fields[5]);
                if(x1<0||x1>=1272||x2<0||x2>=1272||y1<0||y1>=1696||y2<0||y2>=1696||duration<100||duration>2000)throw new IllegalArgumentException("swipe bounds");
                new Thread(new Runnable(){public void run(){try{
                    Class<?> manager=Class.forName("android.hardware.input.InputManager");Object input=manager.getMethod("getInstance").invoke(null);
                    Method inject=manager.getMethod("injectInputEvent",InputEvent.class,int.class);long down=SystemClock.uptimeMillis();
                    for(int n=0;n<=12;n++){int action=n==0?MotionEvent.ACTION_DOWN:n==12?MotionEvent.ACTION_UP:MotionEvent.ACTION_MOVE;
                        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,x1+(x2-x1)*n/12,y1+(y2-y1)*n/12,0);
                        event.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);inject.invoke(input,event,0);event.recycle();if(n<12)Thread.sleep(duration/12);}
                }catch(Throwable error){Log.e(TAG,"CONTROL_SWIPE_FAILED",error);}}},"kindle-swipe-test").start();
            }
            Log.i(TAG,"CONTROL_COMMAND="+request);
        }catch(Throwable e){Log.e(TAG,"CONTROL_COMMAND_FAILED "+request,e);}
    }
    static void startExternalActivity(Intent intent)throws Exception{
        Object am=Class.forName("android.app.ActivityManagerNative").getMethod("getDefault").invoke(null);
        for(Method method:Class.forName("android.app.IActivityManager").getMethods()){
            if(method.getName().equals("startActivityAsUser")&&method.getParameterTypes().length==11){
                Object result=method.invoke(am,new Object[]{null,"android",intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),null,null,null,0,0,null,null,0});
                Log.i(TAG,"CONTROL_ACTIVITY_START result="+result+" component="+intent.getComponent());return;
            }
        }
        throw new NoSuchMethodException("IActivityManager.startActivityAsUser");
    }
    static String uninstallUserApp(final String pkg){try{
        if(!pkg.matches("[A-Za-z0-9_.]+")||pkg.equals("org.kindlelab.apkprobe"))return "DENIED";
        android.content.pm.ApplicationInfo info=context.getPackageManager().getApplicationInfo(pkg,0);
        if((info.flags&android.content.pm.ApplicationInfo.FLAG_SYSTEM)!=0)return "DENIED";
        final java.util.concurrent.CountDownLatch done=new java.util.concurrent.CountDownLatch(1);
        final int[] result={Integer.MIN_VALUE};
        final Binder callback=new Binder(){protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException{
            if(code==IBinder.INTERFACE_TRANSACTION){reply.writeString("android.content.pm.IPackageDeleteObserver");return true;}
            if(code==1){data.enforceInterface("android.content.pm.IPackageDeleteObserver");String deleted=data.readString();int status=data.readInt();if(pkg.equals(deleted)){result[0]=status;done.countDown();}if(reply!=null)reply.writeNoException();return true;}
            return super.onTransact(code,data,reply,flags);
        }};
        final Class<?> observerType=Class.forName("android.content.pm.IPackageDeleteObserver");
        Object observer=java.lang.reflect.Proxy.newProxyInstance(observerType.getClassLoader(),new Class[]{observerType},new InvocationHandler(){public Object invoke(Object proxy,Method method,Object[] args){if(method.getName().equals("asBinder"))return callback;return null;}});
        IBinder binder=(IBinder)Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"package");
        Object pm=Class.forName("android.content.pm.IPackageManager$Stub").getMethod("asInterface",IBinder.class).invoke(null,binder);
        pm.getClass().getMethod("deletePackageAsUser",String.class,observerType,int.class,int.class).invoke(pm,pkg,observer,0,0);
        if(!done.await(20,java.util.concurrent.TimeUnit.SECONDS))return "TIMEOUT";
        Log.i(TAG,"USER_APP_UNINSTALL package="+pkg+" result="+result[0]);return result[0]==1?"OK":"FAILED";
    }catch(Throwable e){Log.e(TAG,"USER_APP_UNINSTALL_FAILED "+pkg,e);return "FAILED";}}
    static String forceStopUserApp(final String pkg){
        try{
            if(!pkg.matches("[A-Za-z0-9_.]+")||pkg.equals("org.kindlelab.apkprobe")){
                Log.w(TAG,"APP_POLICY_FORCE_STOP_DENIED package="+pkg+" reason=protected");return "DENIED";
            }
            android.content.pm.ApplicationInfo info=context.getPackageManager().getApplicationInfo(pkg,0);
            if((info.flags&android.content.pm.ApplicationInfo.FLAG_SYSTEM)!=0){
                Log.w(TAG,"APP_POLICY_FORCE_STOP_DENIED package="+pkg+" reason=system");return "DENIED";
            }
            Object am=Class.forName("android.app.ActivityManagerNative").getMethod("getDefault").invoke(null);
            Class.forName("android.app.IActivityManager").getMethod("forceStopPackage",String.class,int.class).invoke(am,pkg,0);
            Log.i(TAG,"APP_POLICY_FORCE_STOP package="+pkg+" data_preserved=1");return "OK";
        }catch(Throwable error){Log.e(TAG,"APP_POLICY_FORCE_STOP_FAILED package="+pkg,error);return "FAILED";}
    }
    static void startCommandSocket(){
        new Thread(new Runnable(){public void run(){
            try {LocalServerSocket server=new LocalServerSocket("kindle-controls");
                for(;;){LocalSocket socket=server.accept();
                    try {socket.setSoTimeout(1500);int uid=socket.getPeerCredentials().getUid();
                        if(uid==0||uid==1000||uid==desktopUid){String request=new BufferedReader(new InputStreamReader(socket.getInputStream(),"UTF-8")).readLine();
                            if(request!=null&&request.length()<160){final String action=request;
                                if(action.startsWith("uninstall ")){
                                    final LocalSocket pending=socket;
                                    final String pkg=action.substring(10);
                                    new Thread(new Runnable(){public void run(){try{pending.getOutputStream().write((uninstallUserApp(pkg)+"\n").getBytes("UTF-8"));}catch(Exception e){Log.w(TAG,"UNINSTALL_REPLY_FAILED",e);}finally{try{pending.close();}catch(Exception ignored){}}}},"kindle-delete-app").start();
                                    socket=null;
                                }else if(action.startsWith("force-stop ")){
                                    final String pkg=action.substring(11);
                                    final LocalSocket pending=socket;
                                    new Thread(new Runnable(){public void run(){try{pending.setSoTimeout(4000);pending.getOutputStream().write((forceStopUserApp(pkg)+"\n").getBytes("UTF-8"));}catch(Exception e){Log.w(TAG,"APP_POLICY_FORCE_STOP_REPLY_FAILED",e);}finally{try{pending.close();}catch(Exception ignored){}}}},"kindle-app-policy-stop").start();
                                    socket=null;
                                }else if(action.equals("retain")){
                                    boolean available=new File(TMP+"kindle-warm-available").exists();
                                    write(available?"kindle-android-retain":"kindle-android-exit",Long.toString(System.nanoTime())+"\n");
                                    socket.getOutputStream().write((available?"OK\n":"COLD\n").getBytes("UTF-8"));
                                }else if(action.equals("status")){
                                    java.util.Properties state=new java.util.Properties();
                                    try{FileInputStream in=new FileInputStream(TMP+"kindle-device-status");state.load(in);in.close();}catch(Exception ignored){}
                                    String response=state.getProperty("clock","--:--")+"|"+state.getProperty("battery","--")+"\n";
                                    socket.getOutputStream().write(response.getBytes("UTF-8"));
                                }else{main.post(new Runnable(){public void run(){command(action);}});
                                    socket.getOutputStream().write("ACCEPTED\n".getBytes("UTF-8"));}}
                        }
                    }catch(Throwable error){Log.w(TAG,"CONTROL_CLIENT_DISCONNECTED",error);}finally{try{if(socket!=null)socket.close();}catch(Exception ignored){}}
                }
            }catch(Throwable e){Log.e(TAG,"CONTROL_SOCKET_FAILED",e);}
        }},"kindle-control-socket").start();
    }
    static void readerSettings() throws Exception {
        Class<?> amType=Class.forName("android.app.IActivityManager");
        Object am=Class.forName("android.app.ActivityManagerNative").getMethod("getDefault").invoke(null);
        IBinder token=new Binder();
        Object holder=amType.getMethod("getContentProviderExternal",String.class,int.class,IBinder.class)
                .invoke(am,"settings",0,token);
        Object provider=holder.getClass().getField("provider").get(holder);
        Method call=Class.forName("android.content.IContentProvider").getMethod("call",String.class,String.class,String.class,Bundle.class);
        try {
            String[] keys={"sound_effects_enabled","window_animation_scale","transition_animation_scale","animator_duration_scale","device_provisioned","user_setup_complete","install_non_market_apps"};
            for(int i=0;i<keys.length;i++){
                Bundle extras=new Bundle();extras.putString("value",i>=4?"1":"0");extras.putInt("_user",0);
                call.invoke(provider,"android",i==0?"PUT_system":(i==5||i==6)?"PUT_secure":"PUT_global",keys[i],extras);
            }
            Log.i(TAG,"READER_SOUND_DISABLED=1 window_animations=false hardware_swipe="+animations);
            Bundle user=new Bundle();user.putInt("_user",0);
            String ime="org.kindlelab.apkprobe/.ReaderInputMethod";
            Bundle oldEnabled=(Bundle)call.invoke(provider,"android","GET_secure","enabled_input_methods",user);
            String enabled=oldEnabled==null?null:oldEnabled.getString("value");
            if(enabled==null||!java.util.Arrays.asList(enabled.split(":")).contains(ime)){
                Bundle update=new Bundle();update.putInt("_user",0);update.putString("value",enabled==null||enabled.length()==0?ime:enabled+":"+ime);
                call.invoke(provider,"android","PUT_secure","enabled_input_methods",update);
            }
            Bundle oldDefault=(Bundle)call.invoke(provider,"android","GET_secure","default_input_method",user);
            String selected=oldDefault==null?null:oldDefault.getString("value");
            if(selected==null||selected.length()==0){Bundle update=new Bundle();update.putInt("_user",0);update.putString("value",ime);call.invoke(provider,"android","PUT_secure","default_input_method",update);}
            Bundle awake=new Bundle();awake.putInt("_user",0);awake.putString("value","2147483647");
            call.invoke(provider,"android","PUT_system","screen_off_timeout",awake);
            Log.i(TAG,"READER_IME_ENABLED="+ime+" android_idle_sleep=host_guard");
            write("kindle-display-animation",animations?"1\n":"0\n");
            write("kindle-display-animation-steps",animationSteps+"\n");
        }finally{amType.getMethod("removeContentProviderExternal",String.class,IBinder.class).invoke(am,"settings",token);}
    }
    static ImageView button(String label, final Runnable action) {
        ImageView view=new ImageView(context);
        view.setImageDrawable(new ControlIcon(label));
        view.setScaleType(ImageView.ScaleType.CENTER);
        view.setContentDescription(label);view.setSoundEffectsEnabled(false);
        StateListDrawable states=new StateListDrawable();
        states.addState(new int[]{android.R.attr.state_pressed},new ColorDrawable(0xffdddddd));
        states.addState(new int[0],new ColorDrawable(Color.WHITE));view.setBackground(states);
        view.setOnClickListener(new View.OnClickListener(){public void onClick(View v){action.run();}});
        return view;
    }
    static void pulse(){modeButton.setAlpha(0.35f);main.postDelayed(new Runnable(){public void run(){modeButton.setAlpha(1f);}},250);}
    static void fullRefresh(){try{write("kindle-display-full-refresh","1\n");pulse();Log.i(TAG,"CONTROL_FULL_REFRESH_REQUESTED=1");}catch(Exception e){Log.e(TAG,"REFRESH_FAILED",e);}}
    static void setMode(int value){try{mode=value;write("kindle-display-mode",new String[]{"auto","reading","fast"}[mode]);fullRefresh();Log.i(TAG,"CONTROL_REFRESH_MODE="+mode);}catch(Exception e){Log.e(TAG,"MODE_FAILED",e);}}
    static boolean dismissSettings(){
        if(settingsDialog!=null && settingsDialog.isShowing()){
            settingsDialog.dismiss();Log.i(TAG,"INK_SETTINGS_DISMISSED=1");return true;
        }
        return false;
    }
    static void showDisplayMenu(){
        if(settingsDialog!=null && settingsDialog.isShowing()){dismissSettings();return;}
        final android.app.Dialog dialog=new android.app.Dialog(context);
        settingsDialog=dialog;
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.getWindow().setType(2003);
        dialog.setCancelable(true);dialog.setCanceledOnTouchOutside(true);
        dialog.setOnDismissListener(new android.content.DialogInterface.OnDismissListener(){public void onDismiss(android.content.DialogInterface d){if(settingsDialog==dialog)settingsDialog=null;}});
        dialog.setOnKeyListener(new android.content.DialogInterface.OnKeyListener(){public boolean onKey(android.content.DialogInterface d,int code,KeyEvent event){if(code==KeyEvent.KEYCODE_BACK&&event.getAction()==KeyEvent.ACTION_UP){dialog.dismiss();return true;}return false;}});
        LinearLayout panel=new LinearLayout(context);panel.setOrientation(1);panel.setPadding(dp(18),dp(12),dp(18),dp(14));panel.setBackground(card(Color.WHITE,0xffbbbbbb,16));
        LinearLayout header=new LinearLayout(context);header.setGravity(Gravity.CENTER_VERTICAL);
        TextView heading=menuText("墨水屏设置");heading.setTextSize(23);heading.setTypeface(getMenuTypeface(),android.graphics.Typeface.BOLD);
        header.addView(heading,new LinearLayout.LayoutParams(0,dp(46),1));
        ImageView close=button("关闭",new Runnable(){public void run(){dialog.dismiss();}});close.setContentDescription("关闭设置");close.setBackground(card(0xfff3f3f3,0xffeeeeee,22));
        header.addView(close,new LinearLayout.LayoutParams(dp(44),dp(44)));close.setOnClickListener(new View.OnClickListener(){public void onClick(View v){dialog.dismiss();}});panel.addView(header);
        TextView subtitle=menuText("显示  ·  灯光  ·  网络");subtitle.setTextSize(12);subtitle.setTextColor(0xff777777);panel.addView(subtitle,new LinearLayout.LayoutParams(-1,dp(27)));
        ScrollView scroll=new ScrollView(context);scroll.setFillViewport(false);scroll.setVerticalScrollBarEnabled(false);
        LinearLayout content=new LinearLayout(context);content.setOrientation(1);
        section(content,"刷新模式");
        LinearLayout modes=new LinearLayout(context);final TextView[] picks=new TextView[3];String[] names={"彩色","清晰","快速"};
        for(int i=0;i<3;i++){final int index=i;final TextView choice=menuText(names[i]);picks[i]=choice;choice.setGravity(Gravity.CENTER);choice.setTextSize(16);
            choice.setBackground(card(i==mode?0xff222222:0xfff4f4f4,i==mode?0xff222222:0xffe0e0e0,8));choice.setTextColor(i==mode?Color.WHITE:Color.BLACK);
            LinearLayout.LayoutParams itemLp=new LinearLayout.LayoutParams(0,dp(44),1);itemLp.setMargins(i==0?0:dp(6),0,0,0);modes.addView(choice,itemLp);
            choice.setOnClickListener(new View.OnClickListener(){public void onClick(View v){setMode(index);for(int j=0;j<picks.length;j++){picks[j].setBackground(card(j==mode?0xff222222:0xfff4f4f4,j==mode?0xff222222:0xffe0e0e0,8));picks[j].setTextColor(j==mode?Color.WHITE:Color.BLACK);}}});
        }content.addView(modes);
        TextView modeNote=menuText("彩色保留色彩，清晰适合正文；快速模式可能增加残影。");modeNote.setTextSize(11);modeNote.setTextColor(0xff777777);content.addView(modeNote,new LinearLayout.LayoutParams(-1,dp(36)));
        CompoundButton animationSwitch=settingSwitch("翻页动画",animations);content.addView(animationSwitch,new LinearLayout.LayoutParams(-1,dp(44)));
        animationSwitch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener(){public void onCheckedChanged(CompoundButton view,boolean checked){animations=checked;try{readerSettings();}catch(Exception e){Log.e(TAG,"ANIMATION_SETTING_FAILED",e);}}});
        section(content,"翻页动画步数");
        LinearLayout stepChoices=new LinearLayout(context);final TextView[] stepPicks=new TextView[3];final int[] stepValues={6,12,18};
        for(int i=0;i<stepValues.length;i++){final int index=i;TextView choice=menuText(stepValues[i]+" 步");stepPicks[i]=choice;choice.setGravity(Gravity.CENTER);choice.setTextSize(15);
            boolean selected=stepValues[i]==animationSteps;choice.setBackground(card(selected?0xff222222:0xfff4f4f4,selected?0xff222222:0xffe0e0e0,8));choice.setTextColor(selected?Color.WHITE:Color.BLACK);
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(40),1);lp.setMargins(i==0?0:dp(6),0,0,0);stepChoices.addView(choice,lp);
            choice.setOnClickListener(new View.OnClickListener(){public void onClick(View v){animationSteps=stepValues[index];try{write("kindle-display-animation-steps",animationSteps+"\n");}catch(Exception e){Log.e(TAG,"ANIMATION_STEPS_FAILED",e);}
                for(int j=0;j<stepPicks.length;j++){boolean active=stepValues[j]==animationSteps;stepPicks[j].setBackground(card(active?0xff222222:0xfff4f4f4,active?0xff222222:0xffe0e0e0,8));stepPicks[j].setTextColor(active?Color.WHITE:Color.BLACK);}
                Log.i(TAG,"ANIMATION_STEPS="+animationSteps);
            }});
        }content.addView(stepChoices);
        section(content,"灯光");java.util.Properties state=new java.util.Properties();
        try{FileInputStream input=new FileInputStream(TMP+"kindle-device-status");state.load(input);input.close();}catch(Exception ignored){}
        LinearLayout lights=new LinearLayout(context);lights.setOrientation(1);lights.setPadding(dp(12),dp(4),dp(12),dp(2));lights.setBackground(card(0xfff6f6f6,0xffeeeeee,10));
        addLightSlider(lights,"亮度","brightness",state);addLightSlider(lights,"暖光","warmth",state);content.addView(lights);
        section(content,"网络");final boolean wifi="1".equals(state.getProperty("wifi","1"));
        CompoundButton wireless=settingSwitch("Wi-Fi",wifi);content.addView(wireless,new LinearLayout.LayoutParams(-1,dp(44)));
        wireless.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener(){public void onCheckedChanged(CompoundButton view,boolean enabled){try{write("kindle-device-request","wifi "+(enabled?"1":"0")+"\n");}catch(Exception e){Log.e(TAG,"DEVICE_CONTROL_FAILED",e);}}});
        TextView note=menuText("切换网络请返回 Kindle 设置。关闭 Wi-Fi 会断开联网。");note.setTextSize(11);note.setTextColor(0xff777777);content.addView(note,new LinearLayout.LayoutParams(-1,dp(36)));
        scroll.addView(content);panel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        TextView done=menuText("完成");done.setTextSize(16);done.setGravity(Gravity.CENTER);done.setBackground(card(0xff222222,0xff222222,10));done.setTextColor(Color.WHITE);panel.addView(done,new LinearLayout.LayoutParams(-1,dp(46)));
        done.setOnClickListener(new View.OnClickListener(){public void onClick(View v){dialog.dismiss();}});
        dialog.setContentView(panel);dialog.show();
        Window window=dialog.getWindow();window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);window.setDimAmount(0);window.setWindowAnimations(0);
        int maxHeight=Math.max(dp(360),context.getResources().getDisplayMetrics().heightPixels-dp(150));
        window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));window.setLayout(Math.min(dp(356),context.getResources().getDisplayMetrics().widthPixels-dp(36)),Math.min(maxHeight,dp(620)));
        Log.i(TAG,"INK_SETTINGS_OPENED=1 close_controls=3");
    }
    static Drawable card(int color,int stroke,int radius){android.graphics.drawable.GradientDrawable d=new android.graphics.drawable.GradientDrawable();d.setColor(color);d.setStroke(dp(1),stroke);d.setCornerRadius(dp(radius));return d;}
    static void section(LinearLayout panel,String label){TextView title=menuText(label);title.setTextSize(12);title.setTextColor(0xff777777);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(26));lp.topMargin=dp(8);panel.addView(title,lp);}
    static final class InkSwitch extends CompoundButton {
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        InkSwitch(){super(context);setClickable(true);setFocusable(true);setButtonDrawable((Drawable)null);setBackgroundColor(Color.TRANSPARENT);setSoundEffectsEnabled(false);}
        @Override protected void onDraw(Canvas canvas){
            Paint text=getPaint();text.setColor(Color.BLACK);text.setTypeface(getMenuTypeface());text.setTextSize(dp(16));
            Paint.FontMetrics fm=text.getFontMetrics();canvas.drawText(getText().toString(),0,getHeight()/2f-(fm.ascent+fm.descent)/2,text);
            float right=getWidth()-dp(2),left=right-dp(42),cy=getHeight()/2f;
            paint.setColor(isChecked()?0xff222222:0xffcccccc);canvas.drawRoundRect(new RectF(left,cy-dp(12),right,cy+dp(12)),dp(12),dp(12),paint);
            paint.setColor(Color.WHITE);canvas.drawCircle(isChecked()?right-dp(12):left+dp(12),cy,dp(9),paint);
        }
    }
    static CompoundButton settingSwitch(String label,boolean checked){InkSwitch v=new InkSwitch();v.setText(label);v.setContentDescription(label);v.setChecked(checked);return v;}
    static final class InkSeekBar extends SeekBar {
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        InkSeekBar(){super(context);setPadding(dp(10),0,dp(10),0);setSoundEffectsEnabled(false);}
        @Override protected synchronized void onDraw(Canvas canvas){
            float left=getPaddingLeft(),right=getWidth()-getPaddingRight(),cy=getHeight()/2f;
            float x=left+(right-left)*getProgress()/Math.max(1,getMax());
            paint.setStrokeWidth(dp(3));paint.setStrokeCap(Paint.Cap.ROUND);paint.setColor(0xffcccccc);canvas.drawLine(left,cy,right,cy,paint);
            paint.setColor(0xff222222);canvas.drawLine(left,cy,x,cy,paint);canvas.drawCircle(x,cy,dp(7),paint);
        }
    }
    static android.graphics.Typeface getMenuTypeface(){
        if(menuTypeface==null)try{menuTypeface=android.graphics.Typeface.createFromFile("/system/fonts/NotoSansCJK-Regular.ttc");}catch(Exception ignored){}
        return menuTypeface;
    }
    static TextView menuText(String text){TextView view=new TextView(context);view.setText(text);view.setIncludeFontPadding(false);view.setTextSize(16);view.setTextColor(Color.BLACK);view.setGravity(Gravity.CENTER_VERTICAL);view.setSoundEffectsEnabled(false);
        android.graphics.Typeface typeface=getMenuTypeface();if(typeface!=null)view.setTypeface(typeface);return view;}
    static void addLightSlider(LinearLayout panel,final String label,final String key,java.util.Properties state){
        int current=0;try{current=Integer.parseInt(state.getProperty(key,"0"));}catch(Exception ignored){}
        final TextView title=menuText(label+"   "+current);title.setTextSize(14);panel.addView(title,new LinearLayout.LayoutParams(-1,dp(24)));
        SeekBar bar=new InkSeekBar();bar.setMax(24);bar.setProgress(Math.max(0,Math.min(24,current)));bar.setProgressTintList(android.content.res.ColorStateList.valueOf(0xff333333));bar.setThumbTintList(android.content.res.ColorStateList.valueOf(0xff333333));panel.addView(bar,new LinearLayout.LayoutParams(-1,dp(40)));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar b,int value,boolean user){title.setText(label+"   "+value);}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){try{write("kindle-device-request",key+" "+b.getProgress()+"\n");}catch(Exception e){Log.e(TAG,"DEVICE_CONTROL_FAILED",e);}}});
    }
    static LinearLayout floatingRoot;
    static WindowManager.LayoutParams floatingLayout;
    static ImageView edgeHandle;
    static android.graphics.drawable.GradientDrawable floatingBackground;
    static boolean floatingExpanded;
    static final Runnable collapseFloating=new Runnable(){public void run(){setBarVisible(false);}};
    static void addBar() throws Exception {
        floatingRoot=new LinearLayout(context);floatingRoot.setOrientation(0);
        floatingBackground=new android.graphics.drawable.GradientDrawable();
        floatingBackground.setColor(Color.TRANSPARENT);floatingBackground.setStroke(dp(1),Color.TRANSPARENT);floatingBackground.setCornerRadius(dp(10));floatingRoot.setBackground(floatingBackground);
        edgeHandle=button("返回",new Runnable(){public void run(){}});edgeHandle.setAlpha(1f);edgeHandle.setBackgroundColor(Color.TRANSPARENT);
        edgeHandle.setContentDescription("展开导航，拖动调整位置");floatingRoot.addView(edgeHandle,new LinearLayout.LayoutParams(dp(22),dp(46)));
        LinearLayout row=new LinearLayout(context);
        row.addView(button("返回",new Runnable(){public void run(){key(KeyEvent.KEYCODE_BACK);setBarVisible(false);}}),new LinearLayout.LayoutParams(dp(46),dp(46)));
        row.addView(button("桌面",new Runnable(){public void run(){key(KeyEvent.KEYCODE_HOME);setBarVisible(false);}}),new LinearLayout.LayoutParams(dp(46),dp(46)));
        row.addView(button("全刷",new Runnable(){public void run(){fullRefresh();setBarVisible(false);}}),new LinearLayout.LayoutParams(dp(46),dp(46)));
        modeButton=button("墨水屏设置",new Runnable(){public void run(){setBarVisible(false);showDisplayMenu();}});
        row.addView(modeButton,new LinearLayout.LayoutParams(dp(46),dp(46)));navigationBar=row;row.setVisibility(View.GONE);floatingRoot.addView(row);
        floatingLayout=new WindowManager.LayoutParams(dp(22),dp(46),2003,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
        floatingLayout.gravity=Gravity.RIGHT|Gravity.CENTER_VERTICAL;floatingLayout.y=dp(110);floatingLayout.windowAnimations=0;floatingLayout.setTitle("Kindle edge navigation");
        final WindowManager wm=(WindowManager)context.getSystemService(Context.WINDOW_SERVICE);
        edgeHandle.setOnTouchListener(new View.OnTouchListener(){float startY;int originY;boolean dragged;
            public boolean onTouch(View v,MotionEvent e){
                if(e.getActionMasked()==MotionEvent.ACTION_DOWN){startY=e.getRawY();originY=floatingLayout.y;dragged=false;main.removeCallbacks(collapseFloating);return true;}
                if(e.getActionMasked()==MotionEvent.ACTION_MOVE){int delta=Math.round(e.getRawY()-startY);if(Math.abs(delta)>dp(8))dragged=true;
                    if(dragged){int limit=context.getResources().getDisplayMetrics().heightPixels/2-dp(70);floatingLayout.y=Math.max(-limit,Math.min(limit,originY+delta));wm.updateViewLayout(floatingRoot,floatingLayout);}return true;}
                if(e.getActionMasked()==MotionEvent.ACTION_UP){if(!dragged)setBarVisible(!floatingExpanded);else if(floatingExpanded)main.postDelayed(collapseFloating,2500);return true;}
                if(e.getActionMasked()==MotionEvent.ACTION_CANCEL){setBarVisible(false);return true;}return true;
            }});
        wm.addView(floatingRoot,floatingLayout);
        write("kindle-controls-ready","navigation=edge\n");Log.i(TAG,"SYSTEM_CONTROLS_READY=1 navigation=edge auto_collapse_ms=2500");
    }
    static void setBarVisible(boolean expanded){
        if(navigationBar==null)return;
        main.removeCallbacks(collapseFloating);floatingExpanded=expanded;
        edgeHandle.setAlpha(1f);edgeHandle.setBackgroundColor(Color.TRANSPARENT);
        floatingBackground.setColor(expanded?0x99ffffff:Color.TRANSPARENT);
        floatingBackground.setStroke(dp(1),expanded?0x66888888:Color.TRANSPARENT);
        navigationBar.setVisibility(expanded?View.VISIBLE:View.GONE);edgeHandle.setScaleX(expanded?-1f:1f);
        floatingLayout.width=dp(expanded?206:22);
        ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).updateViewLayout(floatingRoot,floatingLayout);
        if(expanded)main.postDelayed(collapseFloating,2500);
        Log.i(TAG,"EDGE_NAVIGATION_EXPANDED="+expanded);
    }
    static final class SessionAgent extends android.net.NetworkAgent {
        SessionAgent(NetworkInfo info, NetworkCapabilities caps, LinkProperties links, android.net.NetworkMisc misc) {
            super(Looper.myLooper(),context,TAG,info,caps,links,70,misc);
        }
        protected void unwanted(){Log.w(TAG,"SESSION_NETWORK_UNWANTED");}
    }
    static void registerNetwork() {
        try {
            NetworkInfo info=NetworkInfo.class.getConstructor(int.class,int.class,String.class,String.class)
                .newInstance(ConnectivityManager.TYPE_ETHERNET,0,"ETHERNET","Kindle TAP");
            call(info,"setIsAvailable",new Class<?>[]{boolean.class},true);
            call(info,"setDetailedState",new Class<?>[]{NetworkInfo.DetailedState.class,String.class,String.class},NetworkInfo.DetailedState.CONNECTED,null,"Kindle TAP");
            NetworkCapabilities caps=NetworkCapabilities.class.newInstance();
            call(caps,"addTransportType",new Class<?>[]{int.class},NetworkCapabilities.TRANSPORT_ETHERNET);
            for(int capability:new int[]{12,13,14,15}) call(caps,"addCapability",new Class<?>[]{int.class},capability);
            LinkProperties links=LinkProperties.class.newInstance();
            call(links,"setInterfaceName",new Class<?>[]{String.class},"tap0");
            LinkAddress address=LinkAddress.class.getConstructor(String.class).newInstance("10.0.2.100/24");
            call(links,"addLinkAddress",new Class<?>[]{LinkAddress.class},address);
            call(links,"addDnsServer",new Class<?>[]{java.net.InetAddress.class},java.net.InetAddress.getByName("10.0.2.3"));
            RouteInfo route=RouteInfo.class.getConstructor(LinkAddress.class,java.net.InetAddress.class,String.class)
                .newInstance(null,java.net.InetAddress.getByName("10.0.2.2"),"tap0");
            call(links,"addRoute",new Class<?>[]{RouteInfo.class},route);
            RouteInfo local=RouteInfo.class.getConstructor(LinkAddress.class,java.net.InetAddress.class,String.class)
                .newInstance(address,null,"tap0");
            call(links,"addRoute",new Class<?>[]{RouteInfo.class},local);
            android.net.NetworkMisc misc=new android.net.NetworkMisc();
            misc.explicitlySelected=true; misc.acceptUnvalidated=true;
            network=new SessionAgent(info,caps,links,misc);
            Log.i(TAG,"SESSION_NETWORK_AGENT_REGISTERED=1 interface=tap0");
        } catch(Throwable error){Log.e(TAG,"SESSION_NETWORK_AGENT_FAILED",error);}
        probeNetwork();
    }
    static void probeNetwork(){
        new Thread(new Runnable(){public void run(){
            try { Thread.sleep(4000); ConnectivityManager cm=(ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE); Log.i(TAG,"SESSION_ACTIVE_NETWORK="+cm.getActiveNetwork()+" links="+cm.getLinkProperties(cm.getActiveNetwork())); java.net.InetAddress[] addresses=java.net.InetAddress.getAllByName("weread.qq.com");
                Log.i(TAG,"SESSION_ANDROID_DNS_PASS count="+addresses.length);
                javax.net.ssl.HttpsURLConnection connection=(javax.net.ssl.HttpsURLConnection)new java.net.URL("https://weread.qq.com/").openConnection();
                connection.setConnectTimeout(10000); connection.setReadTimeout(10000);
                Log.i(TAG,"SESSION_ANDROID_HTTPS_STATUS="+connection.getResponseCode()); connection.disconnect();
            }catch(Throwable error){Log.e(TAG,"SESSION_ANDROID_NETWORK_PROBE_FAILED "+error.toString()+" cause="+error.getCause(),error);}
        }},"kindle-network-probe").start();
    }
    /** Android 7 IClipboard wire contract; text only, private user 0. */
    static final class TextClipboard extends Binder {
        static final String DESCRIPTOR="android.content.IClipboard";
        android.content.ClipData clip;
        final RemoteCallbackList<IInterface> listeners=new RemoteCallbackList<IInterface>();
        void checkPackage(String pkg){
            int uid=Binder.getCallingUid();
            if(uid==0||uid==1000)return;
            String[] names=context.getPackageManager().getPackagesForUid(uid);
            if(names!=null)for(String name:names)if(name.equals(pkg))return;
            throw new SecurityException("clipboard caller package mismatch");
        }
        synchronized android.content.ClipData snapshot(){return clip==null?null:new android.content.ClipData(clip);}
        void changed(){main.post(new Runnable(){public void run(){
            int count=listeners.beginBroadcast();try{for(int i=0;i<count;i++){
                Parcel message=Parcel.obtain();try{
                    message.writeInterfaceToken("android.content.IOnPrimaryClipChangedListener");
                    listeners.getBroadcastItem(i).asBinder().transact(IBinder.FIRST_CALL_TRANSACTION,message,null,IBinder.FLAG_ONEWAY);
                }catch(RemoteException ignored){}finally{message.recycle();}
            }}finally{listeners.finishBroadcast();}
        }});}
        protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException{
            if(code==IBinder.INTERFACE_TRANSACTION){reply.writeString(DESCRIPTOR);return true;}
            if(code<1||code>7)return super.onTransact(code,data,reply,flags);
            data.enforceInterface(DESCRIPTOR);
            if(code==1){
                android.content.ClipData requested=data.readInt()!=0?android.content.ClipData.CREATOR.createFromParcel(data):null;
                checkPackage(data.readString());
                if(requested==null||requested.getItemCount()!=1||requested.getItemAt(0).getText()==null||
                    requested.getItemAt(0).getUri()!=null||requested.getItemAt(0).getIntent()!=null)
                    throw new IllegalArgumentException("text clipboard requires one text item");
                CharSequence text=requested.getItemAt(0).getText();if(text.length()>65536)throw new IllegalArgumentException("clipboard text too long");
                synchronized(this){clip=android.content.ClipData.newPlainText("text",text.toString());}
                changed();reply.writeNoException();return true;
            }
            if(code==5||code==6){
                final IBinder binder=data.readStrongBinder();if(code==5)checkPackage(data.readString());
                IInterface listener=new IInterface(){public IBinder asBinder(){return binder;}};
                if(binder!=null){if(code==5){if(listeners.getRegisteredCallbackCount()>=128)throw new IllegalStateException("too many clipboard listeners");listeners.register(listener);}else listeners.unregister(listener);}
                reply.writeNoException();return true;
            }
            checkPackage(data.readString());android.content.ClipData value=snapshot();reply.writeNoException();
            if(code==2){reply.writeInt(value==null?0:1);if(value!=null)value.writeToParcel(reply,0);}
            else if(code==3){reply.writeInt(value==null?0:1);if(value!=null)value.getDescription().writeToParcel(reply,0);}
            else reply.writeInt(value!=null&&(code==4||value.getItemAt(0).getText().length()>0)?1:0);
            return true;
        }
    }
    static void ensureTextClipboard(){try{
        Class<?> manager=Class.forName("android.os.ServiceManager");
        if(manager.getMethod("checkService",String.class).invoke(null,"clipboard")==null){
            manager.getMethod("addService",String.class,IBinder.class).invoke(null,"clipboard",new TextClipboard());
            Log.i(TAG,"TEXT_CLIPBOARD_READY=1 implementation=android7-binder-text-only");
        }
    }catch(Throwable e){Log.e(TAG,"TEXT_CLIPBOARD_FAILED",e);}}
    static void grantDesktopStorage(){
        for(String permission:new String[]{"android.permission.READ_EXTERNAL_STORAGE","android.permission.WRITE_EXTERNAL_STORAGE"}){
            try{Object pm=context.getPackageManager();
                if(context.checkPermission(permission,-1,desktopUid)!=android.content.pm.PackageManager.PERMISSION_GRANTED){
                    Constructor<?> ctor=UserHandle.class.getDeclaredConstructor(int.class);
                    ctor.setAccessible(true);
                    Object user=ctor.newInstance(0);
                    pm.getClass().getMethod("grantRuntimePermission",String.class,String.class,UserHandle.class)
                            .invoke(pm,"org.kindlelab.apkprobe",permission,user);
                }
                if(context.checkPermission(permission,-1,desktopUid)!=android.content.pm.PackageManager.PERMISSION_GRANTED)
                    throw new SecurityException("permission remained denied after grant");
                Log.i(TAG,"FILE_MANAGER_PERMISSION_READY="+permission);
            }catch(Throwable e){Log.e(TAG,"FILE_MANAGER_PERMISSION_FAILED "+permission,e);}
        }
    }
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        main=new Handler();
        Class<?> activityThread=Class.forName("android.app.ActivityThread");
        Object thread=activityThread.getMethod("systemMain").invoke(null);
        context=(Context)activityThread.getMethod("getSystemContext").invoke(thread);
        try{desktopUid=context.getPackageManager().getApplicationInfo("org.kindlelab.apkprobe",0).uid;}
        catch(Exception e){Log.e(TAG,"DESKTOP_UID_LOOKUP_FAILED",e);}
        ensureTextClipboard();
        grantDesktopStorage();
        try{BufferedReader saved=new BufferedReader(new FileReader(TMP+"kindle-display-animation-steps"));int value=Integer.parseInt(saved.readLine());saved.close();
            if(value==6||value==12||value==18)animationSteps=value;
        }catch(Exception ignored){}
        try {
            readerSettings();
        }catch(Throwable e){Log.e(TAG,"READER_SETTINGS_FAILED",e);}
        try {addBar();}catch(Throwable e){Log.e(TAG,"SYSTEM_CONTROLS_FAILED",e);}
        startCommandSocket();
        new Handler().postDelayed(new Runnable(){public void run(){registerNetwork();}},3000);
        Looper.loop();
    }
}
