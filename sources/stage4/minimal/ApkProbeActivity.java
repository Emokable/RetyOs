package org.kindlelab.apkprobe;
import android.app.Activity;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.*;
import android.os.Bundle;
import android.util.Log;
import android.view.*;
import android.widget.*;
import java.io.FileOutputStream;
import java.util.*;
import java.text.Collator;

/** Native text-only HOME: no images, animation, WebView or polling. */
public final class ApkProbeActivity extends Activity {
    static final String READER="com.tencent.weread.eink";
    LinearLayout apps; TextView count, deviceStatus;
    final android.os.Handler statusHandler=new android.os.Handler();
    boolean appsDirty=true;
    boolean appsQueryRunning,desktopDestroyed;
    long lastAppsQueryUptime;
    int lastAppCount=-1;
    String appsSignature="";
    final Runnable startupAppsRefresh=new Runnable(){public void run(){appsDirty=true;if(statusActive)listApps();}};
    final Runnable refreshAppsAfterPackageChange=new Runnable(){public void run(){if(statusActive&&!isFinishing())listApps();}};
    final BroadcastReceiver packageChanges=new BroadcastReceiver(){public void onReceive(Context context,Intent intent){
        appsDirty=true;
        statusHandler.removeCallbacks(refreshAppsAfterPackageChange);
        statusHandler.postDelayed(refreshAppsAfterPackageChange,350);
    }};
    final BroadcastReceiver userStateChanges=new BroadcastReceiver(){public void onReceive(Context context,Intent intent){
        appsDirty=true;statusHandler.removeCallbacks(refreshAppsAfterPackageChange);
        statusHandler.post(refreshAppsAfterPackageChange);
        Log.i("KindleAndroidDesktop","APP_LIST_INVALIDATED action="+intent.getAction());
    }};
    boolean statusActive;
    String activePackage,backgroundPackage;
    static final String SAVED_BACKGROUND="saved_background_package";
    static final String SAVED_ACTIVE="saved_active_package";
    void rememberBackground(String pkg){
        backgroundPackage=pkg;
        getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE).edit().putString(SAVED_BACKGROUND,pkg).apply();
    }
    boolean memoryTightForAnotherApp(){
        // One bounded read at launch, never a polling/cleanup thread.
        java.io.BufferedReader input=null;
        try{
            input=new java.io.BufferedReader(new java.io.FileReader("/proc/meminfo"));
            long available=-1,swapFree=-1;String line;
            while((line=input.readLine())!=null){
                if(line.startsWith("MemAvailable:"))available=Long.parseLong(line.trim().split("\\s+")[1]);
                else if(line.startsWith("SwapFree:"))swapFree=Long.parseLong(line.trim().split("\\s+")[1]);
            }
            boolean tight=available>=0&&(available<131072||(available<262144&&swapFree>=0&&swapFree<32768));
            Log.i("KindleAndroidDesktop","APP_MEMORY_GATE available_kib="+available+" swap_free_kib="+swapFree+" reclaim_previous="+tight);
            return tight;
        }catch(Exception error){Log.w("KindleAndroidDesktop","APP_MEMORY_GATE_UNAVAILABLE",error);return false;}
        finally{try{if(input!=null)input.close();}catch(Exception ignored){}}
    }
    boolean awaitingReturn,launcherStoppedForApp;
    boolean appLaunchPending,appCleanupPending,discardUntilFreshDown;
    String queuedLaunchPackage;ResolveInfo queuedLaunchInfo;int queuedLaunchGeneration;
    static final String FAILED_STOPS="failed_app_stops";
    final Set<String> failedStops=new HashSet<String>();
    interface StopCallback { void done(boolean ok); }
    void clearQueuedLaunch(){queuedLaunchPackage=null;queuedLaunchInfo=null;}
    void drainQueuedLaunch(){
        String pkg=queuedLaunchPackage;ResolveInfo info=queuedLaunchInfo;int generation=queuedLaunchGeneration;
        clearQueuedLaunch();
        if(pkg!=null&&generation==operationGeneration&&statusActive&&!desktopDestroyed)launch(pkg,info);
    }
    void finishCleanup(boolean ok){
        appCleanupPending=false;listApps();
        if(ok)drainQueuedLaunch();else{clearQueuedLaunch();if(statusActive&&!desktopDestroyed)Toast.makeText(this,"上一个应用暂未结束，请重试",Toast.LENGTH_SHORT).show();}
    }
    int operationGeneration;
    long returnTouchCutoffUptime,acceptedTouchDownTime=-1;
    boolean touchSequenceAccepted;
    final Runnable statusTick=new Runnable(){public void run(){
        if(!statusActive)return;
        new Thread(new Runnable(){public void run(){
            String result=null;android.net.LocalSocket socket=new android.net.LocalSocket();
            try{socket.connect(new android.net.LocalSocketAddress("kindle-controls",android.net.LocalSocketAddress.Namespace.ABSTRACT));socket.setSoTimeout(1500);
                socket.getOutputStream().write("status\n".getBytes("UTF-8"));
                String reply=new java.io.BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),"UTF-8")).readLine();
                if(reply!=null&&reply.indexOf('|')>0)result=reply.replace("|","   ·   ")+"%";
            }catch(Exception error){Log.w("KindleAndroidDesktop","STATUS_QUERY_RETRY "+error.toString());}
            finally{try{socket.close();}catch(Exception ignored){}}
            final String label=result;
            statusHandler.post(new Runnable(){public void run(){if(!statusActive)return;
                if(label!=null&&!label.equals(deviceStatus.getText().toString()))deviceStatus.setText(label);
                statusHandler.postDelayed(statusTick,label==null?2000:30000);
            }});
        }},"kindle-desktop-status").start();
    }};
    int dp(float v){return Math.round(v*getResources().getDisplayMetrics().density);}
    TextView text(String label,int size){TextView v=new TextView(this);v.setText(label);v.setTextSize(size);v.setTextColor(Color.BLACK);v.setSoundEffectsEnabled(false);return v;}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        android.content.SharedPreferences lifecycle=getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE);
        backgroundPackage=lifecycle.getString(SAVED_BACKGROUND,null);
        failedStops.addAll(lifecycle.getStringSet(FAILED_STOPS,Collections.<String>emptySet()));
        activePackage=lifecycle.getString(SAVED_ACTIVE,null);
        awaitingReturn=activePackage!=null;launcherStoppedForApp=awaitingReturn;
        if(!lifecycle.getBoolean("warm_lifecycle_v1",false))lifecycle.edit().putString(AppPolicyActivity.MODE,AppPolicyActivity.ONE_BACKGROUND).putBoolean(AppPolicyActivity.STOP_ON_RETURN,false).putBoolean("warm_lifecycle_v1",true).commit();
        // Fresh Android data images may carry the older migration marker while
        // still having the old single-app default. Preserve explicit one/system
        // choices, but upgrade that old default once to the already-tested
        // one-background policy so an app survives a trip to the desktop.
        if(!lifecycle.getBoolean("background_policy_v2",false)){
            String mode=lifecycle.getString(AppPolicyActivity.MODE,AppPolicyActivity.SINGLE);
            if(AppPolicyActivity.SINGLE.equals(mode))lifecycle.edit().putString(AppPolicyActivity.MODE,AppPolicyActivity.ONE_BACKGROUND).commit();
            lifecycle.edit().putBoolean("background_policy_v2",true).commit();
        }
        // The Kindle-side launcher tap can still be queued when Android's
        // first Activity becomes interactive. Do not let that old gesture
        // activate the first app row (WeRead) as the desktop appears.
        returnTouchCutoffUptime=android.os.SystemClock.uptimeMillis();
        discardUntilFreshDown=true;touchSequenceAccepted=false;acceptedTouchDownTime=-1;
        Log.i("KindleAndroidDesktop","STARTUP_TOUCH_BARRIER=cutoff_uptime="+returnTouchCutoffUptime);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Color.WHITE);getWindow().setNavigationBarColor(Color.WHITE);
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(24),dp(32),dp(24),dp(12));page.setBackgroundColor(Color.WHITE);
        TextView title=text("RetyOs",30);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        LinearLayout header=new LinearLayout(this);header.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(title,new LinearLayout.LayoutParams(0,dp(54),1));
        TextView exitButton=text("Kindle",16);exitButton.setGravity(Gravity.CENTER);exitButton.setContentDescription("返回 Kindle 并保留 Android");
        TextView filesButton=text("文件",16);filesButton.setGravity(Gravity.CENTER);
        TextView policyButton=text("后台",16);policyButton.setGravity(Gravity.CENTER);policyButton.setContentDescription("后台应用管理");
        filesButton.setOnClickListener(new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(ApkProbeActivity.this,FilesActivity.class));}});
        policyButton.setOnClickListener(new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(ApkProbeActivity.this,AppPolicyActivity.class));}});
        header.addView(filesButton,new LinearLayout.LayoutParams(dp(64),dp(48)));
        header.addView(policyButton,new LinearLayout.LayoutParams(dp(64),dp(48)));
        header.addView(exitButton,new LinearLayout.LayoutParams(dp(64),dp(48)));
        exitButton.setOnClickListener(new View.OnClickListener(){public void onClick(View v){exit();}});
        page.addView(header);
        LinearLayout statusRow=new LinearLayout(this);statusRow.setGravity(Gravity.CENTER_VERTICAL);
        deviceStatus=text("时间与电量读取中",15);deviceStatus.setTextColor(0xff555555);
        deviceStatus.setBackgroundColor(0x00ffffff);deviceStatus.setPadding(dp(4),0,dp(4),0);
        statusRow.addView(deviceStatus,new LinearLayout.LayoutParams(-1,dp(28)));
        page.addView(statusRow,new LinearLayout.LayoutParams(-1,dp(30)));
        count=text("应用",14);count.setTextColor(0xff666666);
        page.addView(count,new LinearLayout.LayoutParams(-1,dp(42)));
        View line=new View(this);line.setBackgroundColor(Color.BLACK);page.addView(line,new LinearLayout.LayoutParams(-1,dp(1)));
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        apps=new LinearLayout(this);apps.setOrientation(LinearLayout.VERTICAL);scroll.addView(apps);
        page.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        setContentView(page);
        IntentFilter packageFilter=new IntentFilter();
        packageFilter.addAction(Intent.ACTION_PACKAGE_ADDED);packageFilter.addAction(Intent.ACTION_PACKAGE_REMOVED);
        packageFilter.addAction(Intent.ACTION_PACKAGE_CHANGED);packageFilter.addAction(Intent.ACTION_PACKAGE_REPLACED);
        packageFilter.addDataScheme("package");registerReceiver(packageChanges,packageFilter);
        IntentFilter userFilter=new IntentFilter(Intent.ACTION_USER_UNLOCKED);
        userFilter.addAction(Intent.ACTION_BOOT_COMPLETED);userFilter.addAction(Intent.ACTION_EXTERNAL_APPLICATIONS_AVAILABLE);
        registerReceiver(userStateChanges,userFilter);
        // Two bounded checks also cover a missed unlock broadcast during boot.
        statusHandler.postDelayed(startupAppsRefresh,1500);statusHandler.postDelayed(startupAppsRefresh,6000);
        try(FileOutputStream out=createDeviceProtectedStorageContext().openFileOutput("probe.txt",MODE_PRIVATE)){out.write("ANDROID_DESKTOP_ACTIVITY_CREATED\n".getBytes("UTF-8"));}
        catch(Exception e){Log.e("KindleAndroidDesktop","DESKTOP_MARKER_FAILED",e);}
        Log.i("KindleAndroidDesktop","ANDROID_DESKTOP_ACTIVITY_CREATED");
        if(getIntent().getBooleanExtra("kindlelab-autolaunch-weread",false))launch(READER,null);
    }
    @Override protected void onResume(){super.onResume();
        count.setText(lastAppCount>=0?lastAppCount+" 个应用":"应用");
        if(awaitingReturn&&launcherStoppedForApp){final String finished=activePackage;activePackage=null;awaitingReturn=false;launcherStoppedForApp=false;
            getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE).edit().remove(SAVED_ACTIVE).apply();
            ++operationGeneration;appLaunchPending=false;
            returnTouchCutoffUptime=android.os.SystemClock.uptimeMillis();discardUntilFreshDown=true;
            touchSequenceAccepted=false;acceptedTouchDownTime=-1;
            Log.i("KindleAndroidDesktop","APP_RETURN_TOUCH_BARRIER=event_before_resume operation_generation="+operationGeneration);
            if(finished!=null){android.content.SharedPreferences prefs=getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE);
                if(prefs.getBoolean(AppPolicyActivity.STOP_ON_RETURN,false)){
                    rememberBackground(null);
                    appCleanupPending=true;
                    requestForceStop(finished,new StopCallback(){public void done(boolean ok){finishCleanup(ok);Log.i("KindleAndroidDesktop","APP_RETURN_CLEANUP_RESULT package="+finished+" success="+ok);}});
                }
                else{
                    if(AppPolicyActivity.ONE_BACKGROUND.equals(prefs.getString(AppPolicyActivity.MODE,AppPolicyActivity.SINGLE))&&backgroundPackage!=null&&!backgroundPackage.equals(finished)){
                        final String oldBackground=backgroundPackage;appCleanupPending=true;
                        requestForceStop(oldBackground,new StopCallback(){public void done(boolean ok){finishCleanup(ok);Log.i("KindleAndroidDesktop","APP_BACKGROUND_CLEANUP_RESULT package="+oldBackground+" success="+ok);}});
                    }
                    rememberBackground(finished);
                }
            }
        }
        statusActive=true;if(android.os.SystemClock.uptimeMillis()-lastAppsQueryUptime>5000)appsDirty=true;
        listApps();statusHandler.removeCallbacks(statusTick);statusHandler.post(statusTick);}
    @Override protected void onPause(){statusActive=false;clearQueuedLaunch();statusHandler.removeCallbacks(statusTick);statusHandler.removeCallbacks(refreshAppsAfterPackageChange);super.onPause();}
    @Override protected void onDestroy(){desktopDestroyed=true;statusHandler.removeCallbacks(startupAppsRefresh);statusHandler.removeCallbacks(refreshAppsAfterPackageChange);try{unregisterReceiver(packageChanges);unregisterReceiver(userStateChanges);}catch(Exception ignored){}super.onDestroy();}
    @Override protected void onStop(){super.onStop();
        if(appLaunchPending&&!awaitingReturn){appLaunchPending=false;++operationGeneration;activePackage=null;
            Log.i("KindleAndroidDesktop","APP_LAUNCH_CANCELLED reason=desktop_stopped_before_launch operation_generation="+operationGeneration);}
        if(awaitingReturn)launcherStoppedForApp=true;}
    @Override public boolean dispatchTouchEvent(MotionEvent event){
        final int action=event.getActionMasked();
        if(discardUntilFreshDown){
            // Android MotionEvent times use the uptime clock. A queued DOWN
            // delivered after HOME may still have an older downTime.
            if(action!=MotionEvent.ACTION_DOWN)return true;
            final long downTime=event.getDownTime(),eventTime=event.getEventTime();
            if(downTime<=returnTouchCutoffUptime||eventTime<returnTouchCutoffUptime){
                Log.i("KindleAndroidDesktop","APP_RETURN_TOUCH_BARRIER_REJECTED downTime="+downTime+" eventTime="+eventTime+" cutoff="+returnTouchCutoffUptime);
                return true;
            }
            discardUntilFreshDown=false;touchSequenceAccepted=true;acceptedTouchDownTime=downTime;
            Log.i("KindleAndroidDesktop","APP_RETURN_TOUCH_BARRIER_RELEASED= fresh_down downTime="+downTime);
        }else if(action==MotionEvent.ACTION_DOWN){
            touchSequenceAccepted=true;acceptedTouchDownTime=event.getDownTime();
        }else if((action==MotionEvent.ACTION_POINTER_DOWN||action==MotionEvent.ACTION_POINTER_UP)&&
                 (!touchSequenceAccepted||event.getDownTime()!=acceptedTouchDownTime)){
            return true;
        }else if(!touchSequenceAccepted||event.getDownTime()!=acceptedTouchDownTime){
            // Fresh multi-touch events share the primary DOWN time; stale
            // events from a gesture canceled across HOME do not.
            return true;
        }
        if(action==MotionEvent.ACTION_DOWN)Log.i("KindleLatency","TRACE event=touch_delivered trace_id=touch-"+
                event.getDownTime()+" uptime_ns="+(android.os.SystemClock.uptimeMillis()*1000000L)+
                " input_event_uptime_ns="+(event.getEventTime()*1000000L)+" action=down");
        final boolean handled=super.dispatchTouchEvent(event);
        if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){
            if(acceptedTouchDownTime>=0)Log.i("KindleLatency","TRACE event=touch_sequence_end trace_id=touch-"+
                    acceptedTouchDownTime+" uptime_ns="+(event.getEventTime()*1000000L)+" action="+action);
            touchSequenceAccepted=false;acceptedTouchDownTime=-1;
        }
        return handled;
    }    void listApps(){
        if(!appsDirty||apps==null||appsQueryRunning||desktopDestroyed)return;
        appsDirty=false;appsQueryRunning=true;lastAppsQueryUptime=android.os.SystemClock.uptimeMillis();
        final long started=lastAppsQueryUptime;
        new Thread(new Runnable(){public void run(){try{
        final PackageManager pm=getPackageManager();
        // A launcher may appear before user 0 unlocks. Include both Direct Boot
        // classes so the first snapshot cannot permanently omit stored apps.
        List<ResolveInfo> resolved=pm.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER),
                PackageManager.MATCH_DIRECT_BOOT_AWARE|PackageManager.MATCH_DIRECT_BOOT_UNAWARE);
        final Map<String,ResolveInfo> unique=new HashMap<String,ResolveInfo>();
        for(ResolveInfo i:resolved)if(i.activityInfo!=null&&!getPackageName().equals(i.activityInfo.packageName)&&!i.activityInfo.packageName.equals("com.android.providers.downloads.ui")&&!i.activityInfo.packageName.equals("com.android.documentsui"))unique.put(i.activityInfo.packageName,i);
        final List<String> packages=new ArrayList<String>(unique.keySet());
        try{pm.getPackageInfo(READER,0);if(!packages.contains(READER))packages.add(READER);}catch(Exception ignored){}
        final Collator collator=Collator.getInstance(Locale.CHINA);
        Collections.sort(packages,new Comparator<String>(){public int compare(String a,String b){
            if(a.equals(READER))return b.equals(READER)?0:-1;if(b.equals(READER))return 1;
            return collator.compare(label(a,unique.get(a)),label(b,unique.get(b)));}});
        final Map<String,String> labels=new HashMap<String,String>();final StringBuilder signature=new StringBuilder();
        for(String pkg:packages){String name=label(pkg,unique.get(pkg));labels.put(pkg,name);signature.append(pkg).append('\n').append(name).append('\n');}
        final String snapshot=signature.toString();
        statusHandler.post(new Runnable(){public void run(){appsQueryRunning=false;if(desktopDestroyed)return;
        if(!snapshot.equals(appsSignature)){
        appsSignature=snapshot;apps.removeAllViews();
        for(final String pkg:packages){final ResolveInfo info=unique.get(pkg);
            LinearLayout row=new LinearLayout(ApkProbeActivity.this);row.setOrientation(LinearLayout.HORIZONTAL);row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(4),0,dp(4),0);row.setSoundEffectsEnabled(false);
            StateListDrawable bg=new StateListDrawable();bg.addState(new int[]{android.R.attr.state_pressed},new ColorDrawable(0xffe5e5e5));bg.addState(new int[0],new ColorDrawable(Color.WHITE));row.setBackground(bg);
            row.addView(text(labels.get(pkg),21),new LinearLayout.LayoutParams(0,-2,1));
            TextView arrow=text("›",26);arrow.setTextColor(0xff777777);row.addView(arrow);
            apps.addView(row,new LinearLayout.LayoutParams(-1,dp(76)));
            View rule=new View(ApkProbeActivity.this);rule.setBackgroundColor(0xffdddddd);apps.addView(rule,new LinearLayout.LayoutParams(-1,dp(1)));
            row.setOnClickListener(new View.OnClickListener(){public void onClick(View v){launch(pkg,info);}});
            row.setOnLongClickListener(new View.OnLongClickListener(){public boolean onLongClick(View v){AppManagerActivity.uninstall(ApkProbeActivity.this,pkg);return true;}});
        }
        }
        lastAppCount=packages.size();count.setText(lastAppCount+" 个应用");
        Log.i("KindleAndroidDesktop","APP_LIST_SNAPSHOT count="+packages.size()+" query_ms="+(android.os.SystemClock.uptimeMillis()-started)+" packages="+packages);
        if(appsDirty&&statusActive)listApps();
        }});
        }catch(final Exception error){statusHandler.post(new Runnable(){public void run(){appsQueryRunning=false;appsDirty=true;Log.e("KindleAndroidDesktop","APP_LIST_QUERY_FAILED keep_previous_rows=1",error);}});}
        }},"kindle-app-list").start();
    }
    String label(String pkg,ResolveInfo i){return pkg.equals(READER)?"微信读书":i==null?pkg:i.loadLabel(getPackageManager()).toString();}
    void launch(String pkg,ResolveInfo i){
        if(appCleanupPending||appLaunchPending){
            queuedLaunchPackage=pkg;queuedLaunchInfo=i;queuedLaunchGeneration=operationGeneration;
            Log.i("KindleAndroidDesktop","APP_LAUNCH_QUEUED package="+pkg+" generation="+operationGeneration);return;
        }
        Intent intent=getPackageManager().getLaunchIntentForPackage(pkg);
        if(intent==null&&i!=null)intent=new Intent(Intent.ACTION_MAIN).setComponent(new ComponentName(pkg,i.activityInfo.name));
        if(intent==null&&pkg.equals(READER))intent=new Intent(Intent.ACTION_MAIN).setClassName(pkg,"com.tencent.weread.LauncherActivity");
        if(intent==null){Toast.makeText(this,"暂时无法打开此应用",Toast.LENGTH_SHORT).show();return;}
        final Intent launchIntent=intent;final String target=pkg;
        final long requestUptimeNs=android.os.SystemClock.uptimeMillis()*1000000L;
        final String traceId=acceptedTouchDownTime>=0?"touch-"+acceptedTouchDownTime:"launch-"+(requestUptimeNs/1000000L);
        Log.i("KindleLatency","TRACE event=app_launch_request trace_id="+traceId+" package="+target+
                " uptime_ns="+requestUptimeNs);
        final int generation=++operationGeneration;appLaunchPending=true;
        android.content.SharedPreferences prefs=getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE);
        String mode=prefs.getString(AppPolicyActivity.MODE,AppPolicyActivity.ONE_BACKGROUND);
        final ArrayList<String> stop=new ArrayList<String>();
        if(!AppPolicyActivity.SYSTEM.equals(mode))for(String failed:failedStops)if(!failed.equals(target))stop.add(failed);
        final boolean pressureReclaim=AppPolicyActivity.ONE_BACKGROUND.equals(mode)&&memoryTightForAnotherApp();
        if(AppPolicyActivity.SINGLE.equals(mode)||pressureReclaim){
            if(activePackage!=null&&!activePackage.equals(target)&&!stop.contains(activePackage))stop.add(activePackage);
            if(backgroundPackage!=null&&!backgroundPackage.equals(target)&&!backgroundPackage.equals(activePackage)&&!stop.contains(backgroundPackage))stop.add(backgroundPackage);
            rememberBackground(null);
        }else if(AppPolicyActivity.ONE_BACKGROUND.equals(mode)){
            if(activePackage!=null&&!activePackage.equals(target)){
                if(backgroundPackage!=null&&!backgroundPackage.equals(target)&&!backgroundPackage.equals(activePackage)&&!stop.contains(backgroundPackage))stop.add(backgroundPackage);
                rememberBackground(activePackage);
            }else if(activePackage==null&&target.equals(backgroundPackage))rememberBackground(null);
        }
        stopThenLaunch(stop,0,target,launchIntent,generation,traceId,requestUptimeNs);
    }
    void stopThenLaunch(final List<String> packages,final int index,final String target,final Intent intent,final int generation,final String traceId,final long requestUptimeNs){
        if(generation!=operationGeneration||!appLaunchPending){Log.i("KindleAndroidDesktop","APP_LAUNCH_STALE_CALLBACK_DROPPED generation="+generation+" current="+operationGeneration);return;}
        if(index>=packages.size()){
            appLaunchPending=false;
            if(queuedLaunchPackage!=null&&!target.equals(queuedLaunchPackage)){drainQueuedLaunch();return;}
            clearQueuedLaunch();
            activePackage=target;awaitingReturn=true;launcherStoppedForApp=false;
            // Survive desktop recreation without losing track of retained apps.
            getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE).edit().putString(SAVED_ACTIVE,target).apply();
            Log.i("KindleLatency","TRACE event=start_activity trace_id="+traceId+" package="+target+
                    " request_uptime_ns="+requestUptimeNs+" start_uptime_ns="+(android.os.SystemClock.uptimeMillis()*1000000L));
            try{startActivity(intent);}catch(Exception e){activePackage=null;awaitingReturn=false;Toast.makeText(this,"暂时无法打开此应用",Toast.LENGTH_SHORT).show();Log.e("KindleAndroidDesktop","APP_LAUNCH_FAILED "+target,e);}return;
        }
        requestForceStop(packages.get(index),new StopCallback(){public void done(boolean ok){
            if(generation!=operationGeneration)return;
            if(!ok){appLaunchPending=false;clearQueuedLaunch();Toast.makeText(ApkProbeActivity.this,"上一个应用暂未结束，请重试",Toast.LENGTH_SHORT).show();return;}
            stopThenLaunch(packages,index+1,target,intent,generation,traceId,requestUptimeNs);
        }});
    }
    void requestForceStop(final String pkg,final StopCallback completion){new Thread(new Runnable(){public void run(){android.net.LocalSocket socket=new android.net.LocalSocket();boolean ok=false;try{
        try{getPackageManager().getApplicationInfo(pkg,0);}catch(PackageManager.NameNotFoundException removed){ok=true;return;}
        socket.connect(new android.net.LocalSocketAddress("kindle-controls",android.net.LocalSocketAddress.Namespace.ABSTRACT));socket.setSoTimeout(8000);
        socket.getOutputStream().write(("force-stop "+pkg+"\n").getBytes("UTF-8"));
        String result=new java.io.BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),"UTF-8")).readLine();
        ok="OK".equals(result);
        if(!"OK".equals(result))Log.w("KindleAndroidDesktop","APP_POLICY_FORCE_STOP_RESULT package="+pkg+" result="+result);
        Log.i("KindleAndroidDesktop","APP_POLICY_FORCE_STOP_REQUESTED package="+pkg);
    }catch(Exception error){Log.e("KindleAndroidDesktop","APP_POLICY_FORCE_STOP_REQUEST_FAILED package="+pkg,error);}finally{
        try{socket.close();}catch(Exception ignored){}final boolean success=ok;
        runOnUiThread(new Runnable(){public void run(){
            if(success)failedStops.remove(pkg);else failedStops.add(pkg);
            getSharedPreferences(AppPolicyActivity.PREFS,MODE_PRIVATE).edit().putStringSet(FAILED_STOPS,new HashSet<String>(failedStops)).apply();
            if(completion!=null)completion.done(success);
        }});
    }}},"kindle-app-policy").start();}
    void exit(){retain();}
    void retain(){count.setText("正在返回 Kindle，保留阅读会话…");new Thread(new Runnable(){public void run(){android.net.LocalSocket socket=new android.net.LocalSocket();try{
        socket.connect(new android.net.LocalSocketAddress("kindle-controls",android.net.LocalSocketAddress.Namespace.ABSTRACT));socket.setSoTimeout(2000);
        socket.getOutputStream().write("retain\n".getBytes("UTF-8"));String reply=new java.io.BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),"UTF-8")).readLine();
        if(!"OK".equals(reply)&&!"COLD".equals(reply))throw new java.io.IOException("retain refused");
        statusHandler.postDelayed(new Runnable(){public void run(){listApps();}},1000);
    }catch(Exception e){runOnUiThread(new Runnable(){public void run(){count.setText("返回失败，请重试");}});}finally{try{socket.close();}catch(Exception ignored){}}}},"kindle-retain").start();}
    @Override public void onBackPressed(){}
}
