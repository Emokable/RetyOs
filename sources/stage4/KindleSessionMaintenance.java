import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.os.IBinder;
import android.system.Os;
import java.io.File;
import java.lang.reflect.*;
public final class KindleSessionMaintenance {
 public static void main(String[] args)throws Exception{
  if(args.length<2)throw new IllegalArgumentException();
  String pkg=args[1];if(!pkg.matches("[A-Za-z0-9_.]+"))throw new IllegalArgumentException();
  if(args[0].equals("dump")){
   if(!java.util.Arrays.asList("activity","gfxinfo","SurfaceFlinger","input","meminfo","package").contains(pkg))throw new IllegalArgumentException("unsupported diagnostic service");
   IBinder service=(IBinder)Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,pkg);
   if(service==null)throw new IllegalStateException("service unavailable: "+pkg);
   service.dump(java.io.FileDescriptor.out,java.util.Arrays.copyOfRange(args,2,args.length));return;
  }
  if(args[0].equals("installpolicy")){
   if(!pkg.equals("speed")&&!pkg.equals("interpret-only"))throw new IllegalArgumentException("unsupported install policy");
   Class<?> props=Class.forName("android.os.SystemProperties");
   Method set=props.getMethod("set",String.class,String.class),get=props.getMethod("get",String.class);
   String profiles=pkg.equals("speed")?"true":"false";
   set.invoke(null,"pm.dexopt.install",pkg);set.invoke(null,"dalvik.vm.usejitprofiles",profiles);
   for(int i=0;i<20;i++){
    if(pkg.equals(get.invoke(null,"pm.dexopt.install"))&&profiles.equals(get.invoke(null,"dalvik.vm.usejitprofiles"))){
     System.out.println("INSTALL_POLICY="+pkg+" JIT_PROFILES="+profiles+" VERIFIED=1");return;
    }
    Thread.sleep(100);
   }
   throw new IllegalStateException("property update not acknowledged");
  }
  if(args[0].equals("clipboardtest")){
   IBinder binder=(IBinder)Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"clipboard");
   if(binder==null)throw new IllegalStateException("clipboard missing");
   android.os.Parcel data=android.os.Parcel.obtain(),reply=android.os.Parcel.obtain();
   data.writeInterfaceToken("android.content.IClipboard");data.writeInt(1);android.content.ClipData.newPlainText("test","kindle-clipboard-check").writeToParcel(data,0);data.writeString("android");binder.transact(1,data,reply,0);reply.readException();data.recycle();reply.recycle();
   data=android.os.Parcel.obtain();reply=android.os.Parcel.obtain();data.writeInterfaceToken("android.content.IClipboard");data.writeString("android");binder.transact(2,data,reply,0);reply.readException();
   if(reply.readInt()==0||!"kindle-clipboard-check".contentEquals(android.content.ClipData.CREATOR.createFromParcel(reply).getItemAt(0).getText()))throw new IllegalStateException("clipboard roundtrip failed");data.recycle();reply.recycle();
   System.out.println("CLIPBOARD_TEXT_ROUNDTRIP_PASS=1");return;
  }
  if(args[0].equals("compile")){
   Object binder=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"package");
   Object pm=Class.forName("android.content.pm.IPackageManager$Stub").getMethod("asInterface",IBinder.class).invoke(null,binder);
   for(Method method:pm.getClass().getMethods())if(method.getName().equals("performDexOptMode")){
    System.out.println("DEXOPT_METHOD="+method);
    if(java.util.Arrays.equals(method.getParameterTypes(),new Class[]{String.class,boolean.class,String.class,boolean.class})){
     Object result=method.invoke(pm,pkg,false,"speed",true);System.out.println("DEXOPT_SPEED_RESULT="+result);return;
    }
   }
   throw new IllegalStateException("unsupported dexopt API");
  }
  if(!args[0].equals("dbcheck")||args.length!=3)throw new IllegalArgumentException();
  int uid=Integer.parseInt(args[2]);if(uid<10000)throw new IllegalArgumentException();
  if(android.os.Process.myUid()!=uid)throw new IllegalStateException("launch with session-run-as");
  File directory=new File("/data/user/0/"+pkg+"/databases");File[] files=directory.listFiles();
  if(files==null)throw new IllegalStateException("database directory unreadable");
  int checked=0;
  for(File file:files){if(!file.isFile()||file.getName().endsWith("-wal")||file.getName().endsWith("-shm")||file.getName().endsWith("-journal"))continue;
   SQLiteDatabase db=SQLiteDatabase.openDatabase(file.getPath(),null,SQLiteDatabase.OPEN_READONLY|SQLiteDatabase.NO_LOCALIZED_COLLATORS);
   try{Cursor cursor=db.rawQuery("PRAGMA quick_check(1)",null);try{while(cursor.moveToNext()){String result=cursor.getString(0);System.out.println("DB_CHECK file="+file.getName()+" result="+result);if(!result.equals("ok"))throw new IllegalStateException("integrity failure");}}finally{cursor.close();}
    try{cursor=db.rawQuery("SELECT locale FROM android_metadata",null);try{if(cursor.moveToFirst())System.out.println("DB_LOCALE file="+file.getName()+" locale="+cursor.getString(0));}finally{cursor.close();}}catch(android.database.sqlite.SQLiteException e){System.out.println("DB_NO_LOCALE_METADATA file="+file.getName());}
   }finally{db.close();}checked++;
  }
  System.out.println("DB_CHECK_PASS count="+checked+" uid="+uid+" mode=read-only");
 }
}
