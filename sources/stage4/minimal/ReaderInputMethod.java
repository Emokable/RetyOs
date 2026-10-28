package org.kindlelab.apkprobe;

import android.inputmethodservice.InputMethodService;
import android.view.View;
import android.view.Gravity;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.StateListDrawable;
import android.widget.*;
import android.text.InputType;
import android.util.Log;
import java.io.*;
import java.util.*;

/** Small offline IME: no network, native library, prediction worker, or animations. */
public final class ReaderInputMethod extends InputMethodService {
    final Map<String,String[]> dictionary=new HashMap<String,String[]>();
    final StringBuilder composing=new StringBuilder();
    LinearLayout root,keys,candidates; TextView composition;
    boolean chinese=true,symbols=false,shift=false,password=false;
    int candidatePage=0;String matched="";
    final ArrayList<String> choices=new ArrayList<String>();
    int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    @Override public void onCreate(){super.onCreate();
        try{BufferedReader in=new BufferedReader(new InputStreamReader(getAssets().open("pinyin.tsv"),"UTF-8"));
            String line;while((line=in.readLine())!=null){int at=line.indexOf('\t');if(at>0)dictionary.put(line.substring(0,at),line.substring(at+1).split(" "));}in.close();
            Log.i("ReaderIME","DICTIONARY_READY keys="+dictionary.size());
        }catch(Exception e){Log.e("ReaderIME","DICTIONARY_FAILED",e);}
    }
    @Override public boolean onEvaluateFullscreenMode(){return false;}
    @Override public View onCreateInputView(){
        root=new LinearLayout(this);root.setOrientation(1);root.setBackgroundColor(Color.WHITE);
        root.setPadding(dp(5),dp(3),dp(5),dp(4));
        composition=new TextView(this);composition.setTextColor(Color.BLACK);composition.setTextSize(16);composition.setPadding(dp(9),0,0,0);
        root.addView(composition,new LinearLayout.LayoutParams(-1,dp(26)));
        candidates=new LinearLayout(this);candidates.setGravity(Gravity.CENTER_VERTICAL);
        root.addView(candidates,new LinearLayout.LayoutParams(-1,dp(46)));
        keys=new LinearLayout(this);keys.setOrientation(1);root.addView(keys);
        renderKeys();updateCandidates();return root;
    }
    @Override public void onStartInput(EditorInfo info,boolean restarting){super.onStartInput(info,restarting);
        composing.setLength(0);candidatePage=0;
        int type=info.inputType,variation=type&InputType.TYPE_MASK_VARIATION;
        password=variation==InputType.TYPE_TEXT_VARIATION_PASSWORD||variation==InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD||variation==InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD;
        symbols=(type&InputType.TYPE_MASK_CLASS)==InputType.TYPE_CLASS_NUMBER||(type&InputType.TYPE_MASK_CLASS)==InputType.TYPE_CLASS_PHONE;
        chinese=!password&&variation!=InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS&&variation!=InputType.TYPE_TEXT_VARIATION_URI;
        shift=false;if(keys!=null){renderKeys();updateCandidates();}
    }
    @Override public void onFinishInput(){composing.setLength(0);super.onFinishInput();}
    TextView key(final String label,final Runnable action){TextView v=new TextView(this);
        v.setText(label);v.setTextSize(label.length()>3?15:21);v.setTextColor(Color.BLACK);v.setGravity(Gravity.CENTER);v.setSoundEffectsEnabled(false);
        StateListDrawable bg=new StateListDrawable();bg.addState(new int[]{android.R.attr.state_pressed},new ColorDrawable(0xffc8c8c8));bg.addState(new int[0],new ColorDrawable(0xffeeeeee));v.setBackground(bg);
        v.setOnClickListener(new View.OnClickListener(){public void onClick(View view){action.run();}});return v;}
    void add(LinearLayout row,String label,float weight,Runnable action){LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(45),weight);lp.setMargins(dp(2),dp(2),dp(2),dp(2));row.addView(key(label,action),lp);}
    void renderKeys(){keys.removeAllViews();
        String[] rows=symbols?new String[]{"1234567890","@#￥%&*()-+","，。？！：；/._"}:new String[]{"qwertyuiop","asdfghjkl","zxcvbnm"};
        for(String chars:rows){LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER);
            if(!symbols&&chars.equals("zxcvbnm"))add(row,shift?"⇧":"↑",1.2f,new Runnable(){public void run(){shift=!shift;renderKeys();}});
            for(int i=0;i<chars.length();i++){final String c=chars.substring(i,i+1);add(row,shift?c.toUpperCase(Locale.US):c,1,new Runnable(){public void run(){letter(c);}});}
            if(chars.equals(rows[rows.length-1]))add(row,"⌫",1.4f,new Runnable(){public void run(){backspace();}});
            keys.addView(row,new LinearLayout.LayoutParams(-1,dp(49)));
        }
        LinearLayout row=new LinearLayout(this);
        add(row,symbols?"ABC":"123",1.4f,new Runnable(){public void run(){finishRaw();symbols=!symbols;renderKeys();updateCandidates();}});
        add(row,chinese?"中":"EN",1.1f,new Runnable(){public void run(){finishRaw();chinese=!chinese;renderKeys();updateCandidates();}});
        add(row,"空格",3,new Runnable(){public void run(){if(composing.length()>0&&!choices.isEmpty())commitChoice(0);else if(composing.length()>0)finishRaw();else commit(" ");}});
        add(row,"↵",1.2f,new Runnable(){public void run(){finishRaw();InputConnection ic=getCurrentInputConnection();if(ic!=null){int action=getCurrentInputEditorInfo().imeOptions&EditorInfo.IME_MASK_ACTION;if(action!=EditorInfo.IME_ACTION_NONE&&action!=EditorInfo.IME_ACTION_UNSPECIFIED)ic.performEditorAction(action);else ic.commitText("\n",1);}}});
        add(row,"收起",1.4f,new Runnable(){public void run(){finishRaw();requestHideSelf(0);}});
        keys.addView(row,new LinearLayout.LayoutParams(-1,dp(49)));
    }
    void letter(String c){if(chinese&&!symbols&&!password&&c.matches("[a-z]")&&composing.length()<32){composing.append(c);candidatePage=0;setComposition();}
        else{finishRaw();commit(shift?c.toUpperCase(Locale.US):c);}}
    void commit(String text){InputConnection ic=getCurrentInputConnection();if(ic!=null)ic.commitText(text,1);}
    void finishRaw(){if(composing.length()>0){commit(composing.toString());composing.setLength(0);InputConnection ic=getCurrentInputConnection();if(ic!=null)ic.finishComposingText();}candidatePage=0;updateCandidates();}
    void backspace(){if(composing.length()>0){composing.deleteCharAt(composing.length()-1);candidatePage=0;setComposition();}else{InputConnection ic=getCurrentInputConnection();if(ic!=null)ic.deleteSurroundingText(1,0);}}
    void setComposition(){InputConnection ic=getCurrentInputConnection();if(ic!=null)ic.setComposingText(composing,1);updateCandidates();}
    void updateCandidates(){if(candidates==null)return;candidates.removeAllViews();choices.clear();matched="";
        String input=composing.toString();String[] values=dictionary.get(input);
        if(values!=null){matched=input;Collections.addAll(choices,values);}else if(input.length()>0){
            // A committed syllable leaves the rest of a phrase in composition.
            for(int n=Math.min(6,input.length());n>0;n--){values=dictionary.get(input.substring(0,n));if(values!=null){matched=input.substring(0,n);Collections.addAll(choices,values);break;}}
        }
        composition.setText(input.length()>0?input:(chinese?"简体拼音 · 离线输入":"English"));
        int begin=candidatePage*5;if(begin>=choices.size()){candidatePage=0;begin=0;}
        for(int i=begin;i<Math.min(begin+5,choices.size());i++){final int index=i;add(candidates,choices.get(i),1,new Runnable(){public void run(){commitChoice(index);}});}
        if(choices.size()>5)add(candidates,"›",0.65f,new Runnable(){public void run(){candidatePage++;updateCandidates();}});
    }
    void commitChoice(int index){if(index<0||index>=choices.size())return;String word=choices.get(index);int consumed=matched.length();
        InputConnection ic=getCurrentInputConnection();if(ic!=null){ic.commitText(word,1);ic.finishComposingText();}
        composing.delete(0,Math.min(consumed,composing.length()));candidatePage=0;
        if(composing.length()>0)setComposition();else updateCandidates();
        Log.i("ReaderIME","CANDIDATE_COMMITTED characters="+word.length());
    }
}
