"""Extract the complete Android 7.1 ARMv7 WebView provider from API-25 image."""
from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / 'images/api25/system.img'
OUT = ROOT / 'artifacts/webview-api25/system'
if not IMAGE.is_file():
    raise SystemExit(f'missing {IMAGE}; run scripts/extract-api25.py first')

sys.path.insert(0, str(ROOT / 'toolchains/python-libs'))
from dissect.extfs import ExtFS

files = {
    '/app/WebViewGoogle/WebViewGoogle.apk': 'app/WebViewGoogle/WebViewGoogle.apk',
    '/app/WebViewGoogle/oat/arm/WebViewGoogle.odex': 'app/WebViewGoogle/oat/arm/WebViewGoogle.odex',
    '/lib/libwebviewchromium_loader.so': 'lib/libwebviewchromium_loader.so',
    '/lib/libwebviewchromium_plat_support.so': 'lib/libwebviewchromium_plat_support.so',
    '/framework/framework-res.apk': 'framework/framework-res.apk',
}

with IMAGE.open('rb') as image:
    fs = ExtFS(image)
    for source, relative in files.items():
        target = OUT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with fs.get(source).open() as src, target.open('wb') as dst:
            while True:
                block = src.read(1024 * 1024)
                if not block:
                    break
                dst.write(block)
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        print(f'WEBVIEW_ASSET={relative} bytes={target.stat().st_size} sha256={digest}')

apk = OUT / 'app/WebViewGoogle/WebViewGoogle.apk'
odex = OUT / 'app/WebViewGoogle/oat/arm/WebViewGoogle.odex'
data = odex.read_bytes()
if b'oat\n' not in data[:65536]:
    raise SystemExit('API-25 WebView odex has no ART OAT header in its ELF image')
if b'WebViewChromiumFactoryProvider' not in data:
    raise SystemExit('API-25 WebView odex does not contain the required factory class')
if not apk.is_file() or not data:
    raise SystemExit('API-25 WebView provider extraction is incomplete')
print(f'WEBVIEW_API25_ARMV7_READY=1 apk_bytes={apk.stat().st_size} odex_bytes={len(data)}')
