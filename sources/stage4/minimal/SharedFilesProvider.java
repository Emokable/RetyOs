package org.kindlelab.apkprobe;

import android.content.*;
import android.database.*;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;
import java.io.*;
import java.util.*;

/** Read-only, per-URI sharing; never exposes private app/container files. */
public final class SharedFilesProvider extends ContentProvider {
    static final String AUTHORITY="org.kindlelab.apkprobe.files";
    static File root() throws IOException {return Environment.getExternalStorageDirectory().getCanonicalFile();}
    static File checked(File file) throws IOException {
        File canonical=file.getCanonicalFile();String base=root().getPath();
        if(!canonical.getPath().equals(base)&&!canonical.getPath().startsWith(base+File.separator))throw new SecurityException("共享存储以外的路径");
        return canonical;
    }
    static Uri uri(File file) throws IOException {return new Uri.Builder().scheme("content").authority(AUTHORITY).appendPath(checked(file).getPath()).build();}
    File file(Uri uri) throws FileNotFoundException {
        try{if(!AUTHORITY.equals(uri.getAuthority())||uri.getPathSegments().size()!=1)throw new IOException("无效路径");
            File f=checked(new File(uri.getLastPathSegment()));if(!f.isFile())throw new IOException("文件不存在");return f;
        }catch(Exception e){throw new FileNotFoundException(e.getMessage());}
    }
    static String mime(File file){String name=file.getName();int dot=name.lastIndexOf('.');String ext=dot<0?"":name.substring(dot+1).toLowerCase(Locale.ROOT);
        if(ext.equals("apk"))return "application/vnd.android.package-archive";
        String type=MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);return type==null?"application/octet-stream":type;
    }
    public boolean onCreate(){return true;}
    public String getType(Uri uri){try{return mime(file(uri));}catch(Exception e){return "application/octet-stream";}}
    public ParcelFileDescriptor openFile(Uri uri,String mode) throws FileNotFoundException {
        if(!"r".equals(mode))throw new FileNotFoundException("仅支持读取");return ParcelFileDescriptor.open(file(uri),ParcelFileDescriptor.MODE_READ_ONLY);
    }
    public Cursor query(Uri uri,String[] projection,String selection,String[] args,String order){
        try{File f=file(uri);String[] columns=projection==null?new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE}:projection;
            MatrixCursor cursor=new MatrixCursor(columns);Object[] row=new Object[columns.length];
            for(int i=0;i<columns.length;i++)row[i]=OpenableColumns.DISPLAY_NAME.equals(columns[i])?f.getName():OpenableColumns.SIZE.equals(columns[i])?Long.valueOf(f.length()):null;
            cursor.addRow(row);return cursor;
        }catch(Exception e){return null;}
    }
    public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
    public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
    public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
}
