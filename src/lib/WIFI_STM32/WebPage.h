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
    "html,body{height:100%;margin:0;background-color:#eee;"
    "font-family:system-ui,Avenir,Helvetica,Arial,sans-serif;"
    "color:#666;line-height:1.65em;"
    "-webkit-font-smoothing:antialiased}"

    /* header bar — matches .elrs-header gradient */
    ".header{background-image:linear-gradient(45deg,#9dc66b 5%,#4fa49a 30%,#4361c2);"
    "color:rgba(255,255,255,.75);padding:10px 15px;"
    "font-size:20px;font-weight:400;"
    "width:100%;box-sizing:border-box}"

    /* mui-panel equivalent */
    ".panel{background-color:#fff;padding:15px;margin-bottom:20px;"
    "border-radius:0;box-shadow:0 2px 2px 0 rgba(0,0,0,.16),"
    "0 0 2px 0 rgba(0,0,0,.12)}"
    ".panel-title{font-size:20px;font-weight:400}"

    /* content area */
    ".content{padding:20px 15px}"

    /* drop zone — matches .drop-zone in elrs.css */
    ".drop-zone{font-weight:bold;text-align:center;padding:1em 0;"
    "margin:1em 0;color:#555;border:2px dashed #555;border-radius:7px;"
    "cursor:pointer;transition:all .2s ease-in-out}"
    ".drop-zone.dragover{color:#f00;border-color:#f00;"
    "border-style:solid;box-shadow:inset 0 3px 4px #888}"
    ".drop-zone input{display:none}"

    /* button — matches .mui-btn .mui-btn--primary */
    ".btn{display:inline-block;font-weight:500;font-size:14px;"
    "padding:8px 26px;margin-top:8px;border:none;border-radius:2px;"
    "cursor:pointer;color:#fff;background-color:#2196F3;"
    "text-transform:uppercase;letter-spacing:.03em;"
    "transition:box-shadow .2s}"
    ".btn:hover,.btn:focus{background-color:#39a1f4;"
    "box-shadow:0 0 2px rgba(0,0,0,.12),0 2px 2px rgba(0,0,0,.2)}"
    ".btn:disabled{opacity:.6;cursor:not-allowed}"

    /* progress bar */
    "progress{width:100%;height:16px;margin-top:14px}"

    /* status text */
    "#status{margin-top:12px;font-weight:400;font-size:16px}"
    ".ok{color:#2dd284} .bad{color:#d85261}"

    /* footer — matches ESP footer */
    ".footer{background-color:#0288D1;color:#fff;padding:6px 15px;"
    "font-size:13px;text-align:center;position:fixed;bottom:0;"
    "left:0;right:0}"
    ".footer a{color:#fff;text-decoration:underline}"

    "</style></head><body>"

    /* header */
    "<div class=\"header\">TitanLRS</div>"

    "<div class=\"content\">"

    /* title panel */
    "<div class=\"panel panel-title\">Firmware Update</div>"

    /* main panel */
    "<div class=\"panel\">"
    "<p>Select the correct <strong>firmware.bin</strong> for your "
    "platform otherwise a bad flash may occur.</p>"

    /* file select button + drop zone (mirrors ESP file-drop component) */
    "<button class=\"btn\" onclick=\"document.getElementById('fw').click()\">"
    "Select firmware file</button>"
    "<div class=\"drop-zone\" id=\"dropzone\">"
    "or drop firmware file here"
    "<input id=\"fw\" type=\"file\" accept=\".bin,application/octet-stream\">"
    "</div>"

    "<h3 id=\"status\"></h3>"
    "<progress id=\"bar\" value=\"0\" max=\"100\"></progress>"
    "</div>"
    "</div>"

    /* footer */
    "<div class=\"footer\">STM32H7 Bank-Swap OTA</div>"
    "<script>"
    "var $=function(id){return document.getElementById(id);};"
    "var S=function(t,ok){var e=$('status');e.textContent=t;"
    "e.className=ok===null?'':(ok?'ok':'bad');};"
    "var busy=false;"

    // --- Drop zone drag-and-drop support ---
    "var dz=$('dropzone');"
    "dz.ondragover=function(e){e.preventDefault();dz.classList.add('dragover');};"
    "dz.ondragleave=function(){dz.classList.remove('dragover');};"
    "dz.ondrop=function(e){e.preventDefault();dz.classList.remove('dragover');"
    "if(e.dataTransfer.files.length)startUpload(e.dataTransfer.files[0]);};"

    // --- File input change handler ---
    "$('fw').onchange=function(){if(this.files[0])startUpload(this.files[0]);};"

    // --- Two-phase upload function ---
    "function startUpload(f){"
    "if(busy)return;"
    "if(!f.name.endsWith('.bin')){"
    "S('Please select a .bin file.',false);return;}"
    "busy=true;$('bar').value=0;"

    // Phase 1 — Erase (no body, just a header with the image size)
    "S('Erasing flash (up to 30 s) ...',null);"
    "var ex=new XMLHttpRequest();"
    "ex.open('POST','/erase');"
    "ex.setRequestHeader('X-Image-Size',''+f.size);"
    "ex.timeout=120000;"
    "ex.onload=function(){"
    "if(ex.status!==200){"
    "S('Erase failed: '+ex.responseText,false);busy=false;return;}"

    // Phase 2 — Upload firmware binary
    "S('Uploading firmware ...',null);"
    "var ux=new XMLHttpRequest();"
    "ux.open('POST','/upload');"
    "ux.setRequestHeader('Content-Type','application/octet-stream');"
    "ux.timeout=300000;"
    "ux.upload.onprogress=function(e){"
    "if(e.lengthComputable)$('bar').value=Math.round(100*e.loaded/e.total);};"
    "ux.onload=function(){"
    "busy=false;"
    "if(ux.status===200){$('bar').value=100;"
    "S('Update complete \\u2014 device is rebooting ...',true);}"
    "else{S('Upload failed: '+ux.responseText,false);}};"
    "ux.onerror=function(){busy=false;"
    "S('Upload network error.',false);};"
    "ux.ontimeout=function(){busy=false;"
    "S('Upload timed out.',false);};"
    "ux.send(f);"
    "};"  // end ex.onload (erase success → start upload)

    "ex.onerror=function(){busy=false;"
    "S('Erase network error.',false);};"
    "ex.ontimeout=function(){busy=false;"
    "S('Erase timed out.',false);};"
    "ex.send();"
    "}"  // end startUpload

    "</script></body></html>";

#endif
