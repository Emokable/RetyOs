package org.kindlelab.apkprobe;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import java.util.*;
import java.text.Collator;

public final class AppManagerActivity extends Activity {
    LinearLayout rows;
    static void uninstall(final Activity activity,final String pkg){
        try{ApplicationInfo info=activity.getPackageManager().getApplicationInfo(pkg,0);
            if(pkg.equals(activity.getPackageName())||(info.flags&ApplicationInfo.FLAG_SYSTEM)!=0){Toast.makeText(activity,"这是内置应用",Toast.LENGTH_SHORT).show();return;}
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
    public void onCreate(Bundle saved){super.onCreate(saved);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setBackgroundColor(Color.WHITE);
        TextView title=text("‹  应用管理",24);title.setOnClickListener(new View.OnClickListener(){public void onClick(View v){finish();}});page.addView(title);
        page.addView(text("点击卸载后安装的应用，内置应用保留。",14));ScrollView scroll=new ScrollView(this);rows=new LinearLayout(this);rows.setOrientation(1);scroll.addView(rows);page.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));setContentView(page);
    }
    protected void onResume(){super.onResume();refreshApps();}
    void refreshApps(){rows.removeAllViews();final PackageManager pm=getPackageManager();List<ApplicationInfo> apps=new ArrayList<ApplicationInfo>();
        for(ApplicationInfo app:pm.getInstalledApplications(0))if((app.flags&ApplicationInfo.FLAG_SYSTEM)==0&&!app.packageName.equals(getPackageName()))apps.add(app);
        final Collator order=Collator.getInstance(Locale.CHINA);Collections.sort(apps,new Comparator<ApplicationInfo>(){public int compare(ApplicationInfo a,ApplicationInfo b){return order.compare(a.loadLabel(pm).toString(),b.loadLabel(pm).toString());}});
        if(apps.isEmpty())rows.addView(text("尚未安装其他应用。\n可在「文件」中选择 APK 安装。",17));
        for(final ApplicationInfo app:apps){TextView row=text(app.loadLabel(pm)+"\n"+app.packageName+"    ·    卸载",18);row.setMinHeight(110);rows.addView(row);row.setOnClickListener(new View.OnClickListener(){public void onClick(View v){uninstall(AppManagerActivity.this,app.packageName);}});}
    }
}
