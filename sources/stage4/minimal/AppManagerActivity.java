package org.kindlelab.apkprobe;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import java.util.*;
import java.text.Collator;

public final class AppManagerActivity extends Activity {
    AppList list;
    static void uninstall(final Activity activity,final String pkg){
        try{ApplicationInfo info=activity.getPackageManager().getApplicationInfo(pkg,0);
            if(pkg.equals(activity.getPackageName())||(info.flags&ApplicationInfo.FLAG_SYSTEM)!=0){Toast.makeText(activity,"这是内置应用，不能卸载",Toast.LENGTH_SHORT).show();return;}
            new AlertDialog.Builder(activity).setTitle("卸载 "+info.loadLabel(activity.getPackageManager())+"？").setMessage("应用及其数据将被删除，原 APK 文件保留。").setNegativeButton("取消",null).setPositiveButton("卸载",new DialogInterface.OnClickListener(){public void onClick(DialogInterface d,int which){
                final ProgressDialog progress=ProgressDialog.show(activity,"","正在卸载…",true,true);
                new Thread(new Runnable(){public void run(){String outcome="卸载失败，请重试";android.net.LocalSocket socket=new android.net.LocalSocket();
                    try{socket.connect(new android.net.LocalSocketAddress("kindle-controls",android.net.LocalSocketAddress.Namespace.ABSTRACT));socket.setSoTimeout(25000);
                        socket.getOutputStream().write(("uninstall "+pkg+"\n").getBytes("UTF-8"));
                        String response=new java.io.BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),"UTF-8")).readLine();
                        outcome="OK".equals(response)?"卸载完成":"卸载未完成，请稍后重试";
                    }catch(Exception e){android.util.Log.e("KindleApps","UNINSTALL_REQUEST_FAILED",e);}finally{try{socket.close();}catch(Exception ignored){}}
                    final String message=outcome;activity.runOnUiThread(new Runnable(){public void run(){if(activity.isFinishing())return;progress.dismiss();Toast.makeText(activity,message,Toast.LENGTH_LONG).show();if(activity instanceof ApkProbeActivity)((ApkProbeActivity)activity).listApps();else if(activity instanceof AppManagerActivity)((AppManagerActivity)activity).refreshApps();}});
                }},"kindle-uninstall").start();
            }}).show();
        }catch(Exception e){Toast.makeText(activity,"无法读取应用信息",Toast.LENGTH_LONG).show();}
    }
    TextView text(String value,int size){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(Color.BLACK);t.setPadding(20,20,20,20);return t;}
    int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    public void onCreate(Bundle saved){super.onCreate(saved);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setBackgroundColor(Color.WHITE);
        TextView title=text("‹  应用管理",24);title.setOnClickListener(new View.OnClickListener(){public void onClick(View v){finish();}});page.addView(title);
        page.addView(text("这里列出可卸载的应用（后安装的第三方应用）；番茄、微信读书等内置应用不在此列。",14));
        // 2026-11-06 第七十轮：列表换成单个自绘 View（学桌面 RowList）。旧实现每个应用 new
        // TextView（interpret-only 下 ~2s/个）且查询+排序在主线程，n 个应用就是 1-4 分钟。
        list=new AppList(this);page.addView(list,new LinearLayout.LayoutParams(-1,0,1));setContentView(page);
        DisplayRefresh.onVisible(this);
    }
    protected void onResume(){super.onResume();refreshApps();}
    void refreshApps(){list.setStatus("正在加载应用列表…");
        final PackageManager pm=getPackageManager();
        new Thread(new Runnable(){public void run(){
            final java.util.ArrayList<String> labels=new java.util.ArrayList<String>();
            final java.util.ArrayList<String> pkgs=new java.util.ArrayList<String>();
            final java.util.ArrayList<Boolean> builtin=new java.util.ArrayList<Boolean>();
            try{List<ApplicationInfo> apps=new ArrayList<ApplicationInfo>();
                // 只列可卸载的第三方应用：/system/app 里的内置包（番茄/微信读书/WebView…）有上百个，
                // 全列出来既滚不动也没意义，内置应用统一排除。
                for(ApplicationInfo app:pm.getInstalledApplications(0))if(!app.packageName.equals(getPackageName())&&(app.flags&ApplicationInfo.FLAG_SYSTEM)==0)apps.add(app);
                final Collator order=Collator.getInstance(Locale.CHINA);
                Collections.sort(apps,new Comparator<ApplicationInfo>(){public int compare(ApplicationInfo a,ApplicationInfo b){return order.compare(a.loadLabel(pm).toString(),b.loadLabel(pm).toString());}});
                for(ApplicationInfo app:apps){String label;try{label=app.loadLabel(pm).toString();}catch(Exception e){label=app.packageName;}
                    labels.add(label);pkgs.add(app.packageName);builtin.add((app.flags&ApplicationInfo.FLAG_SYSTEM)!=0);}
            }catch(Exception e){android.util.Log.e("KindleApps","REFRESH_FAILED",e);}
            // 标签在后台线程解析完再上屏，主线程只做一次自绘列表赋值
            final java.util.ArrayList<String> finalLabels=labels;final java.util.ArrayList<String> finalPkgs=pkgs;final java.util.ArrayList<Boolean> finalBuiltin=builtin;
            runOnUiThread(new Runnable(){public void run(){if(isFinishing())return;list.setData(finalLabels,finalPkgs,finalBuiltin);}});
        }},"kindle-apps-list").start();
    }
    // 单个自绘列表：一行 = 应用名(20sp) + 包名/状态(13sp 灰)，按下反色，点击弹卸载确认。
    final class AppList extends View {
        final java.util.ArrayList<String> labels=new java.util.ArrayList<String>();
        final java.util.ArrayList<String> pkgs=new java.util.ArrayList<String>();
        final java.util.ArrayList<Boolean> builtin=new java.util.ArrayList<Boolean>();
        String status="正在加载应用列表…";
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        final int rowHeight;int pressed=-1,downY=0,lastY=0,scrollY=0,scrollStart=0,maxScroll=0;
        boolean dragging=false;
        AppList(android.content.Context context){super(context);rowHeight=dp(76);setBackgroundColor(Color.WHITE);}
        void setStatus(String value){status=value;labels.clear();pkgs.clear();builtin.clear();scrollY=0;maxScroll=0;invalidate();}
        void setData(java.util.ArrayList<String> l,java.util.ArrayList<String> p,java.util.ArrayList<Boolean> b){labels.clear();labels.addAll(l);pkgs.clear();pkgs.addAll(p);builtin.clear();builtin.addAll(b);
            scrollY=0;status=pkgs.isEmpty()?"没有可卸载的应用（内置应用不能卸载）":null;invalidate();
            // 数据上屏后再补刷（太早会被过渡帧吃掉，2026-11-06 实测）。
            postDelayed(new Runnable(){public void run(){DisplayRefresh.request(AppList.this);}},500);}
        int indexAt(float y){int index=(int)((y+scrollY)/rowHeight);return index<0||index>=pkgs.size()?-1:index;}
        @Override protected void onMeasure(int widthSpec,int heightSpec){
            // 窗口高度铺满（不留黑底），超出部分靠 scrollY 自己滚，不依赖外层 ScrollView。
            final int width=MeasureSpec.getSize(widthSpec);
            final int available=MeasureSpec.getSize(heightSpec);
            setMeasuredDimension(width,available);
            maxScroll=Math.max(0,rowHeight*pkgs.size()-available);
            if(scrollY>maxScroll)scrollY=maxScroll;
        }
        @Override protected void onDraw(Canvas canvas){
            final float density=getResources().getDisplayMetrics().scaledDensity;
            if(pkgs.isEmpty()){if(status!=null){paint.setColor(0xff777777);paint.setTextSize(16*density);
                canvas.drawText(status,dp(12),dp(48),paint);}return;}
            final int first=Math.max(0,scrollY/rowHeight);
            final int last=Math.min(pkgs.size(),(scrollY+getHeight())/rowHeight+1);
            for(int i=first;i<last;i++){
                final int top=i*rowHeight-scrollY;
                if(i==pressed){paint.setColor(0xffe5e5e5);canvas.drawRect(0,top,getWidth(),top+rowHeight,paint);}
                paint.setColor(Color.BLACK);paint.setTextSize(20*density);paint.setTextAlign(Paint.Align.LEFT);
                canvas.drawText(labels.get(i),dp(12),top+dp(34),paint);
                paint.setColor(0xff777777);paint.setTextSize(13*density);
                canvas.drawText(pkgs.get(i)+(builtin.get(i)?"    ·    内置应用":"    ·    点击卸载"),dp(12),top+dp(58),paint);
                paint.setColor(0xffdddddd);canvas.drawRect(0,top+rowHeight-1,getWidth(),top+rowHeight,paint);
            }
        }
        @Override public boolean onTouchEvent(MotionEvent event){
            final int action=event.getActionMasked();
            if(action==MotionEvent.ACTION_DOWN){downY=(int)event.getY();lastY=downY;scrollStart=scrollY;dragging=false;pressed=indexAt(event.getY());if(pressed>=0)invalidate();return true;}
            if(action==MotionEvent.ACTION_MOVE){
                final int y=(int)event.getY();
                if(!dragging&&Math.abs(y-downY)>dp(14)){dragging=true;pressed=-1;invalidate();}
                if(dragging){
                    scrollY=scrollStart-(y-downY);
                    if(scrollY<0)scrollY=0;
                    if(scrollY>maxScroll)scrollY=maxScroll;
                    invalidate();
                }
                lastY=y;return true;
            }
            if(action==MotionEvent.ACTION_UP){
                final int index=pressed;pressed=-1;invalidate();
                if(!dragging&&index>=0&&index==indexAt(event.getY())&&index<pkgs.size()){
                    android.util.Log.i("KindleTouch","APPMGR_CLICK package="+pkgs.get(index));
                    if(builtin.get(index))Toast.makeText(AppManagerActivity.this,"内置应用不能卸载",Toast.LENGTH_SHORT).show();
                    else uninstall(AppManagerActivity.this,pkgs.get(index));
                }
                dragging=false;return true;
            }
            if(action==MotionEvent.ACTION_CANCEL){pressed=-1;dragging=false;invalidate();return true;}
            return false;
        }
    }
}
