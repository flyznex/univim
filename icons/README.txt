:uv — macOS icon files
======================

icon_16x16.png        16 × 16
icon_32x32.png        32 × 32
icon_32x32@2x.png     64 × 64
icon_128x128.png     128 × 128
icon_256x256.png     256 × 256
icon_512x512.png     512 × 512
icon_512x512@2x.png 1024 × 1024
menubar_uv@2x.png     88 × 44  (menu bar template, black on transparent)

Build an .icns
--------------
1. Put the seven icon_*.png files in a folder named  uv.iconset
   (add icon_16x16@2x.png = a copy of icon_32x32.png and
    icon_128x128@2x.png = a copy of icon_256x256.png,
    icon_256x256@2x.png = a copy of icon_512x512.png for a complete set)
2. Run:  iconutil -c icns uv.iconset
3. Result: uv.icns  — drop it into your app bundle / Get Info panel.

Menu bar
--------
Name the file  uvTemplate.png / uvTemplate@2x.png  and set
image.isTemplate = true so macOS inverts it in dark mode.
