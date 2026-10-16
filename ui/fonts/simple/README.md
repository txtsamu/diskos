# Squash font additions

Nunito Bold uses the existing Nunito variable source, instantiated at weight 700,
and its OFL licence in ../nowplaying. New text faces are 26 px and 36 px.

The solid icons are Font Awesome 4.7, rendered directly at 36 px for Quick Settings
and feedback, and 80 px for the launcher. No bitmap enlargement is used.
Source: https://raw.githubusercontent.com/FortAwesome/Font-Awesome/v4.7.0/fonts/fontawesome-webfont.ttf
Licence: OFL-1.1-FontAwesome.txt, copied from the existing repository licence.
Only the symbols needed by the theme are rasterized to keep memory bounded.

Rebuild with ui/tools/gen_simple_fonts.py --converter /path/to/lv_font_conv.js.
