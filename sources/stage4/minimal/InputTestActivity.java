package org.kindlelab.apkprobe;
import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.widget.*;
import android.graphics.Color;
import android.text.*;
import android.view.inputmethod.InputMethodManager;
public final class InputTestActivity extends Activity {
 public void onCreate(Bundle saved){super.onCreate(saved);
  android.app.AlarmManager alarms=(android.app.AlarmManager)getSystemService(ALARM_SERVICE);
  alarms.setExact(android.app.AlarmManager.ELAPSED_REALTIME,android.os.SystemClock.elapsedRealtime()+3000,"kindle-listener-regression",new android.app.AlarmManager.OnAlarmListener(){public void onAlarm(){android.util.Log.i("ReaderAlarmTest","LISTENER_DELIVERED=PASS");}},new Handler());
  LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setPadding(32,48,32,24);page.setBackgroundColor(Color.WHITE);
  TextView title=new TextView(this);title.setText("键盘输入测试");title.setTextSize(26);title.setTextColor(Color.BLACK);page.addView(title);
  final EditText field=new EditText(this);field.setSingleLine(true);field.setTextSize(24);field.setHint("输入拼音，点候选汉字");field.setTextColor(Color.BLACK);page.addView(field,new LinearLayout.LayoutParams(-1,100));
  final TextView echo=new TextView(this);echo.setTextSize(22);echo.setTextColor(Color.BLACK);page.addView(echo);
  field.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int st,int c,int a){}public void onTextChanged(CharSequence s,int st,int before,int count){echo.setText(s);android.util.Log.i("ReaderImeTest","TEXT="+s);}public void afterTextChanged(Editable e){}});
  Button done=new Button(this);done.setText("返回桌面");page.addView(done);done.setOnClickListener(new android.view.View.OnClickListener(){public void onClick(android.view.View v){finish();}});
  ScrollView scroll=new ScrollView(this);TextView samples=new TextView(this);samples.setTextColor(Color.BLACK);samples.setTextSize(22);StringBuilder lines=new StringBuilder();for(int n=1;n<=80;n++)lines.append("响应与滑动测试  ").append(n).append("\n\n");samples.setText(lines);scroll.addView(samples);page.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));setContentView(page);field.requestFocus();
  new Handler().postDelayed(new Runnable(){public void run(){((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).showSoftInput(field,InputMethodManager.SHOW_IMPLICIT);}},400);
 }
}
