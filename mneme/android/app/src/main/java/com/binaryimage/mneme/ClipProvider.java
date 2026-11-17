package com.binaryimage.mneme;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/** Read-only door to the clips folder so a clip can be shared to another app (Drive, Messages, email). */
public final class ClipProvider extends ContentProvider {
    static final String AUTH = "com.binaryimage.mneme.clips";

    static Uri uriFor(File f, String niceName) {
        return new Uri.Builder().scheme("content").authority(AUTH).appendPath(f.getName())
                .appendQueryParameter("name", niceName).build();
    }

    private File fileFor(Uri u) throws FileNotFoundException {
        String n = u.getLastPathSegment();
        if (n == null || n.contains("/") || n.contains("..")) throw new FileNotFoundException();
        File f = new File(Store.clipsDir(getContext()), n);
        if (!f.exists()) throw new FileNotFoundException(n);
        return f;
    }

    @Override public boolean onCreate() { return true; }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(fileFor(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public String getType(Uri uri) {
        String n = uri.getLastPathSegment();
        return n != null && n.endsWith(".wav") ? "audio/wav" : "audio/mp4";
    }

    @Override public Cursor query(Uri uri, String[] projection, String sel, String[] args, String sort) {
        MatrixCursor c = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        try {
            File f = fileFor(uri);
            String nice = uri.getQueryParameter("name");
            String ext = f.getName().substring(f.getName().lastIndexOf('.'));
            c.addRow(new Object[]{(nice == null || nice.isEmpty() ? f.getName() : nice.replaceAll("[\\\\/:*?\"<>|]", "_") + ext), f.length()});
        } catch (FileNotFoundException ignored) {
        }
        return c;
    }

    @Override public Uri insert(Uri uri, ContentValues v) { return null; }
    @Override public int delete(Uri uri, String s, String[] a) { return 0; }
    @Override public int update(Uri uri, ContentValues v, String s, String[] a) { return 0; }
}
