package co.logos.webview;

import android.app.Activity;
import android.graphics.Color;
import android.util.Log;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.RenderProcessGoneDetail;

import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * ONE DOWNLOADED MODULE'S PAGE ON ANDROID: a WebView whose every {@code logos:}
 * request is answered by the host's MobileWebBridge.
 *
 * <p>This is the Android half of what {@code IosWebPage.mm} is on iOS, and it is
 * deliberately the same twenty lines of adapter: turn a request into a call,
 * turn the answer into a response, report the death. Everything the container
 * DECIDES -- what a page may fetch, how a frame is framed, how many QML runtimes
 * may live -- is C++ in {@code mobile/webview/} and is tested on a desktop.
 *
 * <p>TWO ANDROID FACTS SHAPE IT.
 *
 * <p>First, {@link WebResourceRequest} has no body. There is no accessor for one
 * and there never was, so a frame from the page cannot ride in a POST body; it
 * rides percent-encoded in the query, split across as many requests as it takes.
 * That is why the shim's sender is chunked (see MobileWebBridge).
 *
 * <p>Second, {@code shouldInterceptRequest} is called on a BACKGROUND thread and
 * is allowed to block. That is what makes the long poll work here with no
 * machinery at all: the native call below simply does not return until the host
 * has frames or the poll expires.
 */
public final class LogosWebPage {

    private static final String TAG = "logos-web";

    /** What one request is answered with. Filled in by the native side. */
    public static final class Reply {
        public int status = 404;
        public String mimeType = "application/octet-stream";
        /** A file to stream, or null. Preferred: the runtime image is 26 MB. */
        public String filePath;
        /** The bytes, when there is no file. */
        public byte[] body;
    }

    /**
     * Answer one request. BLOCKS until the host has an answer, which for a long
     * poll is up to the bridge's park time -- see the class note.
     */
    private static native void nativeServe(long handle, String method, String url, Reply out);

    /** The page is gone: the render process died, or the view was destroyed. */
    private static native void nativeDied(long handle);

    private final long mHandle;
    private WebView mWebView;

    private LogosWebPage(long handle) {
        mHandle = handle;
    }

    /**
     * Build the page and put it in the activity's content view, at the back.
     *
     * <p>Called from the Android UI thread by the native side. IT GOES IN THE
     * HIERARCHY, BEHIND Qt's own surface: a WebView that is not attached is
     * throttled -- requestAnimationFrame stops, and the Qt-wasm runtime draws
     * through it -- so a page that was never mounted would come up and freeze.
     * The shell brings it forward when the user is looking at this module.
     */
    public static LogosWebPage create(Activity activity, long handle, String url,
                                      final String host) {
        LogosWebPage page = new LogosWebPage(handle);
        WebView webView = new WebView(activity);
        page.mWebView = webView;

        webView.getSettings().setJavaScriptEnabled(true);
        // The runtime instantiates two wasm images and draws through WebGL, and
        // a module's storage abstraction lands in IndexedDB, which needs this.
        webView.getSettings().setDomStorageEnabled(true);
        // NOT file access. `logos:` is the only origin a module's page has, and
        // a file:// load is exactly what ADR 0004's loader may not do.
        webView.getSettings().setAllowFileAccess(false);
        webView.getSettings().setAllowContentAccess(false);
        webView.setBackgroundColor(Color.TRANSPARENT);

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view,
                                                              WebResourceRequest request) {
                // BY HOST, not by scheme. A module's page is served over https
                // here (Chromium's Fetch refuses a non-standard scheme inside a
                // WebView, whatever the embedder registered -- see
                // LogosWebPaths.h), so the reserved host is what says "this is
                // ours". Anything else is not intercepted and, having no route,
                // simply fails.
                if (!host.equals(request.getUrl().getHost())) return null;

                Reply reply = new Reply();
                try {
                    nativeServe(handle, request.getMethod(),
                                request.getUrl().toString(), reply);
                } catch (Throwable t) {
                    Log.e(TAG, "the host could not answer " + request.getUrl(), t);
                    return null;
                }

                InputStream stream;
                try {
                    if (reply.filePath != null) {
                        stream = new FileInputStream(reply.filePath);
                    } else {
                        stream = new ByteArrayInputStream(
                            reply.body != null ? reply.body : new byte[0]);
                    }
                } catch (Throwable t) {
                    Log.e(TAG, "could not open " + reply.filePath, t);
                    return null;
                }

                Map<String, String> headers = new HashMap<>();
                // The page fetches its own documents; without this the fetch is
                // blocked as cross-origin and the loader reports nothing useful.
                headers.put("Access-Control-Allow-Origin", "*");
                headers.put("Cache-Control", "no-store");
                return new WebResourceResponse(reply.mimeType, "utf-8", reply.status,
                                               reply.status == 200 ? "OK" : "Error",
                                               headers, stream);
            }

            @Override
            public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
                Log.w(TAG, "the module's render process is gone");
                nativeDied(handle);
                // TRUE, or the whole app is killed with the renderer. Taking the
                // app down with one module's page is precisely what the Web
                // container exists to avoid.
                return true;
            }
        });

        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content != null) {
            content.addView(webView, 0);
        } else {
            Log.w(TAG, "no content view to live in; this page will be throttled");
        }
        webView.loadUrl(url);
        return page;
    }

    /** The WebView, for the shell to position and bring forward. */
    public WebView view() {
        return mWebView;
    }

    /** Tear the page down. Called from the Android UI thread. */
    public void destroy() {
        if (mWebView == null) return;
        mWebView.stopLoading();
        mWebView.setWebViewClient(new WebViewClient());
        ViewGroup parent = (ViewGroup) mWebView.getParent();
        if (parent != null) parent.removeView(mWebView);
        mWebView.destroy();
        mWebView = null;
    }

    public boolean isAlive() {
        return mWebView != null;
    }
}
