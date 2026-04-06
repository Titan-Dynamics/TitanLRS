#ifndef WEBPAGE_H
#define WEBPAGE_H

// ---------------------------------------------------------------------------
// Two-phase OTA web UI
//
// Phase 1: POST /erase  (X-Image-Size header, no body) — erases flash
// Phase 2: POST /upload  (application/octet-stream body) — streams firmware
//
// This prevents the browser from streaming data while the MCU is blocked
// erasing flash (8-32 seconds), which would overflow the WiFi module's
// SPI buffers and kill the connection.
// ---------------------------------------------------------------------------

static const char HTML_BODY[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>STM32H7 Updater</title>"
    "<style>"
    ":root{--bg:#f2f4f8;--card:#ffffff;--ink:#17202a;--muted:#5d6d7e;"
    "--accent:#0b7285;--ok:#1b8f4d;--bad:#c92a2a;}"
    "body{margin:0;background:linear-gradient(150deg,#eaf4ff,#f2f4f8 45%,#eaf7ee);"
    "font-family:Segoe UI,Arial,sans-serif;color:var(--ink)}"
    "main{max-width:620px;margin:32px auto;padding:16px}"
    ".card{background:var(--card);border-radius:14px;padding:22px;"
    "box-shadow:0 8px 24px rgba(0,0,0,.08)}"
    "h1{margin:0 0 6px;font-size:1.5rem}"
    "p{margin:8px 0;color:var(--muted)}"
    "label{display:block;margin-top:14px;font-weight:600}"
    "input,button{width:100%;box-sizing:border-box;margin-top:8px;"
    "padding:10px 12px;border-radius:10px;border:1px solid #ccd3da;font-size:.95rem}"
    "button{background:var(--accent);border:none;color:#fff;font-weight:600;cursor:pointer}"
    "button:disabled{opacity:.6;cursor:not-allowed}"
    "progress{width:100%;height:16px;margin-top:14px}"
    "#status{margin-top:12px;font-weight:600}"
    ".ok{color:var(--ok)} .bad{color:var(--bad)}"
    "</style></head><body><main><div class=\"card\">"
    "<h1>STM32H743 Firmware Update</h1>"
    "<p>Bank-swap OTA &mdash; select a .bin file, click Upload, wait for reboot.</p>"
    "<label>Firmware File (.bin)</label>"
    "<input id=\"fw\" type=\"file\" accept=\".bin,application/octet-stream\">"
    "<button id=\"go\">Upload Firmware</button>"
    "<progress id=\"bar\" value=\"0\" max=\"100\"></progress>"
    "<div id=\"status\"></div>"
    "</div></main>"
    "<script>"
    "var $=function(id){return document.getElementById(id);};"
    "var S=function(t,ok){var e=$('status');e.textContent=t;"
    "e.className=ok===null?'':(ok?'ok':'bad');};"

    // --- Upload button click handler ---
    "$('go').onclick=function(){"
    "var f=$('fw').files[0];"
    "if(!f){S('Pick a .bin file first.',false);return;}"
    "var btn=this;btn.disabled=true;$('bar').value=0;"

    // Phase 1 — Erase (no body, just a header with the image size)
    "S('Erasing flash (up to 30 s) ...',null);"
    "var ex=new XMLHttpRequest();"
    "ex.open('POST','/erase');"
    "ex.setRequestHeader('X-Image-Size',''+f.size);"
    "ex.timeout=120000;"
    "ex.onload=function(){"
    "if(ex.status!==200){"
    "S('Erase failed: '+ex.responseText,false);btn.disabled=false;return;}"

    // Phase 2 — Upload firmware binary
    "S('Uploading firmware ...',null);"
    "var ux=new XMLHttpRequest();"
    "ux.open('POST','/upload');"
    "ux.setRequestHeader('Content-Type','application/octet-stream');"
    "ux.timeout=300000;"
    "ux.upload.onprogress=function(e){"
    "if(e.lengthComputable)$('bar').value=Math.round(100*e.loaded/e.total);};"
    "ux.onload=function(){"
    "btn.disabled=false;"
    "if(ux.status===200){$('bar').value=100;"
    "S('Update complete \\u2014 device is rebooting ...',true);}"
    "else{S('Upload failed: '+ux.responseText,false);}};"
    "ux.onerror=function(){btn.disabled=false;"
    "S('Upload network error.',false);};"
    "ux.ontimeout=function(){btn.disabled=false;"
    "S('Upload timed out.',false);};"
    "ux.send(f);"
    "};"  // end ex.onload (erase success → start upload)

    "ex.onerror=function(){btn.disabled=false;"
    "S('Erase network error.',false);};"
    "ex.ontimeout=function(){btn.disabled=false;"
    "S('Erase timed out.',false);};"
    "ex.send();"
    "};"  // end onclick

    "</script></body></html>";

#endif
