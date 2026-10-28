package org.kindlelab.apkprobe;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.*;

/** Event-driven app cleanup preferences. App data is never cleared. */
public final class AppPolicyActivity extends Activity {
    static final String PREFS="runtime-policy", MODE="mode", STOP_ON_RETURN="stop_on_return";
    static final String SINGLE="single", ONE_BACKGROUND="one_background", SYSTEM="system";
    TextView text(String value,int size){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(Color.BLACK);t.setPadding(20,18,20,18);return t;}
    public void onCreate(Bundle saved){super.onCreate(saved);getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        final android.content.SharedPreferences prefs=getSharedPreferences(PREFS,MODE_PRIVATE);
        LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setBackgroundColor(Color.WHITE);
        TextView back=text("‹  后台应用管理",24);back.setOnClickListener(new View.OnClickListener(){public void onClick(View v){finish();}});page.addView(back);
        page.addView(text("选择阅读应用切换时的后台策略",15));
        final RadioGroup modes=new RadioGroup(this);modes.setOrientation(LinearLayout.VERTICAL);
        final RadioButton single=new RadioButton(this);single.setId(1);single.setText("单应用模式");single.setTextSize(19);single.setPadding(20,12,20,4);modes.addView(single);
        TextView singleHelp=text("切换到另一个应用时结束上一个应用进程。",14);modes.addView(singleHelp);
        final RadioButton one=new RadioButton(this);one.setId(2);one.setText("保留一个后台应用");one.setTextSize(19);one.setPadding(20,12,20,4);modes.addView(one);
        TextView oneHelp=text("内存充足时保留最近一个应用；内存紧张时，切换应用会先回收上一个。",14);modes.addView(oneHelp);
        final RadioButton system=new RadioButton(this);system.setId(3);system.setText("系统默认");system.setTextSize(19);system.setPadding(20,12,20,4);modes.addView(system);
        TextView systemHelp=text("由 Android 自己管理所有后台应用。",14);modes.addView(systemHelp);
        String current=prefs.getString(MODE,ONE_BACKGROUND);modes.check(SYSTEM.equals(current)?3:ONE_BACKGROUND.equals(current)?2:1);
        modes.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener(){public void onCheckedChanged(RadioGroup group,int checked){String value=checked==2?ONE_BACKGROUND:checked==3?SYSTEM:SINGLE;prefs.edit().putString(MODE,value).putBoolean("background_policy_v2",true).commit();}});
        page.addView(modes);
        final CheckBox stopReturn=new CheckBox(this);stopReturn.setText("返回桌面后结束刚才的应用");stopReturn.setTextSize(18);stopReturn.setPadding(20,14,20,14);stopReturn.setChecked(prefs.getBoolean(STOP_ON_RETURN,false));
        stopReturn.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener(){public void onCheckedChanged(CompoundButton button,boolean checked){prefs.edit().putBoolean(STOP_ON_RETURN,checked).commit();}});page.addView(stopReturn);
        TextView note=text("仅结束应用进程，不清除数据库、书籍、设置或下载文件。应用需自行保存阅读进度。",14);note.setTextColor(0xff555555);page.addView(note);
        setContentView(page);
    }
}
