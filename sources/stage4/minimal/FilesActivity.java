package org.kindlelab.apkprobe;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.net.Uri;
import android.os.*;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.text.Collator;
import java.util.*;

/** Text-only shared-storage browser. No thumbnail decoder, service or WebView. */
public final class FilesActivity extends Activity {
    File directory,clipboard;boolean move,busy;TextView path,status,paste;ListView list;
    final Handler ui=new Handler();int generation;
    static final int STORAGE_PERMISSION_REQUEST=71;
    int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    TextView text(String label,int size){TextView v=new TextView(this);v.setText(label);v.setTextSize(size);v.setTextColor(Color.BLACK);v.setGravity(Gravity.CENTER_VERTICAL);v.setPadding(dp(12),dp(10),dp(12),dp(10));v.setSoundEffectsEnabled(false);return v;}
    void toast(String message){Toast.makeText(this,message,Toast.LENGTH_LONG).show();}
    File shared(){return Environment.getExternalStorageDirectory();}
    public void onCreate(Bundle saved){super.onCreate(saved);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setBackgroundColor(Color.WHITE);
        LinearLayout header=new LinearLayout(this);TextView back=text("‹  文件",24),manage=text("应用",17);header.addView(back,new LinearLayout.LayoutParams(0,dp(62),1));header.addView(manage);page.addView(header);
        back.setOnClickListener(new View.OnClickListener(){public void onClick(View v){onBackPressed();}});manage.setOnClickListener(new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(FilesActivity.this,AppManagerActivity.class));}});
        LinearLayout roots=new LinearLayout(this);TextView pc=text("电脑传入",17),internal=text("内部存储",17);roots.addView(pc,new LinearLayout.LayoutParams(0,dp(50),1));roots.addView(internal,new LinearLayout.LayoutParams(0,dp(50),1));page.addView(roots);
        pc.setOnClickListener(new View.OnClickListener(){public void onClick(View v){browse(new File(shared(),"PC"));}});internal.setOnClickListener(new View.OnClickListener(){public void onClick(View v){browse(shared());}});
        page.addView(text("电脑传文件：Kindle 的 AndroidFiles 文件夹",13));path=text("",14);path.setTextColor(0xff555555);page.addView(path);status=text("",14);page.addView(status);
        list=new ListView(this);list.setDividerHeight(dp(1));page.addView(list,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout footer=new LinearLayout(this);TextView refresh=text("刷新",16),create=text("新建文件夹",16);paste=text("粘贴",16);footer.addView(refresh,new LinearLayout.LayoutParams(0,dp(52),1));footer.addView(create,new LinearLayout.LayoutParams(0,dp(52),1));footer.addView(paste,new LinearLayout.LayoutParams(0,dp(52),1));page.addView(footer);setContentView(page);
        refresh.setOnClickListener(new View.OnClickListener(){public void onClick(View v){browse(directory);}});create.setOnClickListener(new View.OnClickListener(){public void onClick(View v){nameDialog(null);}});paste.setOnClickListener(new View.OnClickListener(){public void onClick(View v){paste();}});
        list.setOnItemClickListener(new AdapterView.OnItemClickListener(){public void onItemClick(AdapterView<?> parent,View view,int position,long id){File f=(File)parent.getItemAtPosition(position);if(f.isDirectory())browse(f);else open(f);}});
        list.setOnItemLongClickListener(new AdapterView.OnItemLongClickListener(){public boolean onItemLongClick(AdapterView<?> parent,View view,int position,long id){actions((File)parent.getItemAtPosition(position));return true;}});
        directory=saved==null?new File(shared(),"PC"):new File(saved.getString("directory",shared().getPath()));
        ensureStoragePermission();
    }
    void ensureStoragePermission(){
        if(checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED&&checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED){browse(directory);return;}
        status.setText("需要存储权限才能管理文件");
        requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,Manifest.permission.WRITE_EXTERNAL_STORAGE},STORAGE_PERMISSION_REQUEST);
        Log.i("KindleFiles","STORAGE_PERMISSION_REQUESTED=1");
    }
    public void onRequestPermissionsResult(int requestCode,String[] permissions,int[] grantResults){
        super.onRequestPermissionsResult(requestCode,permissions,grantResults);
        if(requestCode!=STORAGE_PERMISSION_REQUEST)return;
        boolean granted=checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED&&checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED;
        if(granted){Log.i("KindleFiles","STORAGE_PERMISSION_GRANTED=1");browse(directory);}
        else{status.setText("存储权限未授予。请在 Android 设置中允许文件访问后重试。");toast("未获得文件管理权限");Log.e("KindleFiles","STORAGE_PERMISSION_DENIED=1");}
    }
    protected void onSaveInstanceState(Bundle out){super.onSaveInstanceState(out);out.putString("directory",directory.getPath());}
    String folderLabel(File f){try{String relative=f.getAbsolutePath().substring(shared().getAbsolutePath().length());return (relative.equals("/PC")||relative.startsWith("/PC/"))?"电脑传入"+relative.substring(3):"内部存储"+relative;}catch(Exception e){return "文件";}}
    void browse(final File target){if(busy||target==null)return;directory=target.getAbsoluteFile();
        final File current=directory;final int request=++generation;path.setText(folderLabel(current));status.setText("读取中…");
        new Thread(new Runnable(){public void run(){File[] found=null;try{found=SharedFilesProvider.checked(current).listFiles();}catch(Exception ignored){}final List<File> files=new ArrayList<File>();final boolean readable=found!=null;
            if(found!=null){for(File f:found)if(!f.getName().startsWith(".kindle-copy-"))files.add(f);final Collator order=Collator.getInstance(Locale.CHINA);Collections.sort(files,new Comparator<File>(){public int compare(File a,File b){if(a.isDirectory()!=b.isDirectory())return a.isDirectory()?-1:1;return order.compare(a.getName(),b.getName());}});}
            ui.post(new Runnable(){public void run(){if(isFinishing()||request!=generation)return;list.setAdapter(new FileRows(files));Log.i("KindleFiles","FILE_LIST readable="+readable+" count="+files.size());status.setText(!readable?"目录暂不可读，请检查存储权限或重新启动 Android。":files.isEmpty()?"暂无文件":"共 "+files.size()+" 项 · 长按可管理");}});
        }},"kindle-files-list").start();
    }
    final class FileRows extends BaseAdapter {
        final List<File> files;FileRows(List<File> f){files=f;}public int getCount(){return files.size();}public File getItem(int p){return files.get(p);}public long getItemId(int p){return p;}
        public View getView(int p,View old,android.view.ViewGroup parent){TextView row=old instanceof TextView?(TextView)old:text("",18);File f=getItem(p);row.setText((f.isDirectory()?"▸  ":"")+f.getName()+"\n"+(f.isDirectory()?"文件夹":android.text.format.Formatter.formatFileSize(FilesActivity.this,f.length())+(f.getName().toLowerCase(Locale.ROOT).endsWith(".apk")?"  ·  点击安装":"")));row.setMinHeight(dp(70));return row;}
    }
    void open(final File file){try{final File f=SharedFilesProvider.checked(file);if(f.getName().toLowerCase(Locale.ROOT).endsWith(".apk")){
            // Leave archive parsing and installation work to Android's package
            // installer instead of parsing a possibly large APK on this UI thread.
            install(f);
        }else if(f.getName().matches("(?i).*\\.(txt|md|log|json|csv)$")){readText(f);}else{Intent view=new Intent(Intent.ACTION_VIEW).setDataAndType(SharedFilesProvider.uri(f),SharedFilesProvider.mime(f)).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);startActivity(view);}
        }catch(ActivityNotFoundException e){toast("尚未安装可以打开此文件的应用。");}catch(Exception e){toast("无法打开文件："+e.getMessage());}}
    void install(File f){try{Uri uri=SharedFilesProvider.uri(f);Intent intent=new Intent(Intent.ACTION_INSTALL_PACKAGE).setDataAndType(uri,"application/vnd.android.package-archive").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);intent.setClipData(ClipData.newRawUri("APK",uri));intent.putExtra(Intent.EXTRA_RETURN_RESULT,true);toast("正在打开安装器…");startActivityForResult(intent,202);Log.i("KindleFiles","INSTALL_UI_REQUESTED=1 path="+f.getName());}catch(Exception e){toast("无法打开安装器："+e.getMessage());Log.e("KindleFiles","INSTALL_UI_FAILED",e);}}
    protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==202){toast(result==RESULT_OK?"安装完成，可返回桌面打开应用。":"已返回文件列表，请以安装器显示的结果为准。");browse(directory);}}
    void readText(final File f){if(f.length()>256*1024){toast("内置预览支持 256 KB 以内的文本，请用阅读应用打开此文件。");return;}new Thread(new Runnable(){public void run(){try{ByteArrayOutputStream bytes=new ByteArrayOutputStream();FileInputStream in=new FileInputStream(f);try{byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1){if(bytes.size()+n>256*1024)throw new IOException("文本过大");bytes.write(buffer,0,n);}}finally{in.close();}final String value=new String(bytes.toByteArray(),"UTF-8");ui.post(new Runnable(){public void run(){if(isFinishing())return;TextView preview=text(value,16);preview.setTextIsSelectable(true);ScrollView scroll=new ScrollView(FilesActivity.this);scroll.addView(preview);new AlertDialog.Builder(FilesActivity.this).setTitle(f.getName()).setView(scroll).setPositiveButton("关闭",null).show();}});}catch(final Exception e){ui.post(new Runnable(){public void run(){toast("读取失败："+e.getMessage());}});}}},"kindle-text-preview").start();}
    void actions(final File f){new AlertDialog.Builder(this).setTitle(f.getName()).setItems(new String[]{"打开","复制","移动","重命名","删除"},new DialogInterface.OnClickListener(){public void onClick(DialogInterface d,int item){if(item==0){if(f.isDirectory())browse(f);else open(f);}else if(item==1||item==2){if(!f.isFile()){toast("暂仅支持复制或移动单个文件。");return;}clipboard=f;move=item==2;paste.setText("粘贴（1）");toast("请选择目标目录后点击粘贴。");}else if(item==3)nameDialog(f);else confirmDelete(f);}}).show();}
    void confirmDelete(final File f){final boolean folder=f.isDirectory();new AlertDialog.Builder(this).setTitle(folder?"删除文件夹及内容？":"删除此文件？").setMessage(folder?f.getName()+" 及其中所有文件都会被永久删除，无法撤销。":"此文件将被永久删除，无法撤销。").setNegativeButton("取消",null).setPositiveButton("删除",new DialogInterface.OnClickListener(){public void onClick(DialogInterface d,int which){if(busy)return;busy=true;status.setText("删除中…");new Thread(new Runnable(){public void run(){String failure=null;try{deleteTree(f,new int[]{0},0);}catch(Exception e){failure=deleteError(e);}final String result=failure;ui.post(new Runnable(){public void run(){busy=false;if(result==null)toast("删除完成");else toast("删除失败："+result);browse(directory);}});}},"kindle-file-delete").start();}}).show();}
    String deleteError(Exception e){
        if(e instanceof android.system.ErrnoException){int errno=((android.system.ErrnoException)e).errno;return "系统文件操作错误 errno="+errno;}
        return e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
    }
    void deleteTree(File input,int[] count,int depth)throws Exception{if(depth>128||++count[0]>10000)throw new IOException("项目过多或目录层级过深，已停止");File parent=input.getAbsoluteFile().getParentFile();if(parent==null)throw new SecurityException("不能删除存储根目录");File canonicalParent=parent.getCanonicalFile();File root=shared().getCanonicalFile();String base=root.getPath(),parentPath=canonicalParent.getPath();if(!parentPath.equals(base)&&!parentPath.startsWith(base+File.separator))throw new SecurityException("路径超出共享存储");String name=input.getName();if(name.length()==0||name.equals(".")||name.equals(".."))throw new SecurityException("不能删除存储根目录");File target=new File(canonicalParent,name);String targetPath=target.getPath();if(targetPath.equals(base)||targetPath.equals(new File(root,"PC").getPath()))throw new SecurityException("不能删除共享存储或电脑传入目录本身");StructStat st=Os.lstat(targetPath);int type=st.st_mode&OsConstants.S_IFMT;if(type==OsConstants.S_IFDIR){File[] children=target.listFiles();if(children==null)throw new IOException("无法读取文件夹");for(File child:children)deleteTree(new File(targetPath,child.getName()),count,depth+1);}
        if(!target.delete())throw new IOException(type==OsConstants.S_IFDIR?"文件夹删除失败（可能仍有内容或权限不足）":"文件删除失败（权限不足、文件占用或只读存储）");
    }
    void nameDialog(final File original){if(busy)return;final EditText name=new EditText(this);name.setSingleLine(true);if(original!=null){name.setText(original.getName());name.selectAll();}new AlertDialog.Builder(this).setTitle(original==null?"新建文件夹":"重命名").setView(name).setNegativeButton("取消",null).setPositiveButton("确定",new DialogInterface.OnClickListener(){public void onClick(DialogInterface d,int which){try{String value=name.getText().toString().trim();if(value.isEmpty()||value.equals(".")||value.equals("..")||value.indexOf('/')>=0||value.indexOf('\\')>=0)throw new IOException("名称无效");File dest=SharedFilesProvider.checked(new File(directory,value));if(dest.exists())throw new IOException("名称已存在");if(!(original==null?dest.mkdir():SharedFilesProvider.checked(original).renameTo(dest)))throw new IOException("操作失败");browse(directory);}catch(Exception e){toast(e.getMessage());}}}).show();}
    void paste(){if(busy||clipboard==null)return;final File source=clipboard;final boolean moving=move;final File target=new File(directory,source.getName());busy=true;status.setText(moving?"移动中…":"复制中…");
        new Thread(new Runnable(){public void run(){File temp=null;String failure=null;try{SharedFilesProvider.checked(source);SharedFilesProvider.checked(target);if(!source.isFile()||target.exists())throw new IOException("源文件不存在或目标已存在");temp=File.createTempFile(".kindle-copy-",".partial",target.getParentFile());try(FileInputStream in=new FileInputStream(source);FileOutputStream out=new FileOutputStream(temp)){byte[] buffer=new byte[32768];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);out.getFD().sync();}if(target.exists()||!temp.renameTo(target))throw new IOException("无法保存目标文件");temp=null;if(moving&&!source.delete())throw new IOException("已复制，但源文件未能删除");}catch(Exception e){failure=e.getMessage();}finally{if(temp!=null)temp.delete();}final String result=failure;ui.post(new Runnable(){public void run(){busy=false;if(result==null){clipboard=null;paste.setText("粘贴");toast(moving?"移动完成":"复制完成");}else toast(result);browse(directory);}});}},"kindle-file-copy").start();
    }
    public void onBackPressed(){if(busy){toast("文件操作进行中，请稍候。");return;}try{if(directory!=null&&!directory.getAbsolutePath().equals(shared().getAbsolutePath())){browse(directory.getParentFile());return;}}catch(Exception ignored){}super.onBackPressed();}
}
