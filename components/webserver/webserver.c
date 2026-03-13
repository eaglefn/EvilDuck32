#include "webserver.h"
#include "sdcard.h"
#include "duckyscript.h"
#include "wifi_manager.h"
#include "usb_hid.h"
#include "settings.h"
#include "script_storage.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/param.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/socket.h>

static const char *TAG = "webserver";

static int server_socket = -1;
static bool server_running = false;

// ===== HTML/CSS/JS Content (Single Page Application) ===== //

static const char spa_html[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Cache-Control: no-store\r\n"
"Connection: close\r\n"
"\r\n"
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<meta name='theme-color' content='#36393E'>"
"<title>EvilDuck32 Manager</title>"
"<style>"
"body{background:#36393e;margin:0;color:#bfbfbf;font-family:sans-serif}"
"main{width:95vw;max-width:1200px;margin-left:auto;margin-right:auto;padding:0 10px;box-sizing:border-box}"
"section{margin-top:50px}"
"section:first-of-type{margin-top:10px}"
"h1{font-size:1.4rem;margin-top:1rem;background:#2f3136;padding:10px;border-radius:4px;border-left:solid 5px #ffb300;font-weight:100;letter-spacing:5px}"
"h2{font-size:1rem;color:#ffb300;margin:20px 0 6px 0;letter-spacing:2px;text-transform:uppercase}"
"p{margin:.5em 0}"
"a{color:#1ca8eb;text-decoration:none}"
"a:hover{color:#fff;text-decoration:underline dotted}"
"button{padding:10px 20px;border:none;border-radius:4px;background:#2f3136;letter-spacing:.1rem;text-transform:uppercase;margin:2px 0;color:#bfbfbf;cursor:pointer}"
"button:hover{background:#42444a}"
".success{color:#43B581}"
".danger{color:#F04747}"
".warn{color:#ffc107}"
".primary{color:#0092db}"
/* connection LED in title */
"#connled{display:inline-block;width:10px;height:10px;border-radius:50%;background:#43B581;margin-left:8px;vertical-align:middle;box-shadow:0 0 5px #43B581;transition:background .4s,box-shadow .4s}"
"#connled.offline{background:#F04747;box-shadow:0 0 5px #F04747}"
/* run-state badge in control section */
"#stateBadge{display:inline-block;padding:3px 10px;border-radius:3px;font-size:.85rem;background:#23272a;color:#bfbfbf;margin-left:6px;vertical-align:middle}"
"#stateBadge.running{background:#43B581;color:#fff}"
"#stateBadge.paused{background:#ffc107;color:#23272a}"
"#stateBadge.error{background:#F04747;color:#fff}"
".table{width:100%;margin-bottom:2em;border-collapse:collapse}"
"th,td{padding:10px 6px;text-align:left;border-bottom:1px solid #5d5d5d}"
"tr.autorun-row td:first-child::after{content:' \\2605';color:#ffb300}"
"input[type='text'],input[type='file']{color:#bfbfbf;padding:10px;background:#2f3136;border:none;border-radius:4px;margin:2px 0}"
"textarea{color:#bfbfbf;resize:vertical;width:100%;height:400px;padding:15px;border:none;border-radius:4px;background:#2f3136;font-family:'Courier New',Courier,monospace;box-sizing:border-box}"
".debugger{font-family:monospace;background:#2f3136;border-radius:4px;padding:10px;margin-bottom:4px}"
".row{position:relative;width:100%;margin-top:10px}"
".row::after{content:'';display:table;clear:both}"
/* settings table */
"table.stbl{width:100%;max-width:600px;margin-bottom:1em;border-collapse:collapse}"
"table.stbl td{padding:10px;border-bottom:1px solid #5d5d5d}"
/* status bar */
"#status{text-align:center;text-transform:capitalize;padding:5px;color:#fff;background:#2f3136;border-radius:4px;margin-bottom:10px}"
".status-connected{background:#43B581 !important}"
".status-disconnected{background:#F04747 !important}"
/* nav bar */
".nav{background:#2f3136;padding:8px 12px;margin-bottom:10px;display:flex;align-items:center;gap:0}"
".nav a{color:#bfbfbf;text-decoration:none;padding:6px 14px;border-radius:3px;font-size:.9rem;transition:background .15s,color .15s}"
".nav a:hover{color:#fff;background:#42444a;text-decoration:none}"
".nav a.active{color:#ffb300;background:#23272a}"
/* commands page styles */
"table.ctbl{width:100%;border-collapse:collapse;margin-bottom:1.5em;table-layout:fixed}"
"table.ctbl th{background:#23272a;padding:9px 10px;text-align:left;font-size:.8rem;letter-spacing:1px;text-transform:uppercase;color:#ffb300;border-bottom:2px solid #ffb300}"
"table.ctbl td{padding:9px 10px;border-bottom:1px solid #42444a;vertical-align:top}"
"table.ctbl tr:hover td{background:#2a2d31}"
"table.ctbl td:first-child{width:18%;color:#fff;font-family:'Courier New',monospace;font-size:.9rem}"
"table.ctbl td:nth-child(2){width:32%;font-family:'Courier New',monospace;font-size:.82rem;color:#43B581}"
"table.ctbl td:nth-child(3){width:30%;font-size:.88rem}"
"table.ctbl td:last-child{width:20%;font-family:'Courier New',monospace;font-size:.82rem;color:#1ca8eb;word-break:break-all}"
"code{background:#23272a;padding:2px 6px;border-radius:3px;font-family:'Courier New',monospace;font-size:.85rem;color:#43B581}"
".note{background:#2f3136;border-left:3px solid #ffb300;padding:10px 14px;border-radius:0 4px 4px 0;margin:12px 0;font-size:.88rem}"
/* section visibility */
".section{display:none}"
"@media(max-width:768px){"
"main{width:100%;padding:0 5px}"
".table{display:block;overflow-x:auto}"
".table button{padding:6px 10px;font-size:13px}"
"table.ctbl{display:block;overflow-x:auto}"
"table.ctbl td:first-child,table.ctbl td:nth-child(2),table.ctbl td:nth-child(3),table.ctbl td:last-child{width:auto}"
"button{padding:12px 16px;font-size:14px}"
"input,textarea{font-size:16px}"
"}"
"</style></head><body>"

/* ── Nav bar ── */
"<div class='nav'>"
"<a href='#' data-sec='home'     onclick=\"showSection('home');return false;\">EvilDuck32 <span id='connled' title='Connecting...'></span></a>"
"<a href='#' data-sec='sync'     onclick=\"showSection('sync');return false;\">Sync</a>"
"<a href='#' data-sec='settings' onclick=\"showSection('settings');return false;\">Settings</a>"
"<a href='#' data-sec='commands' onclick=\"showSection('commands');return false;\">Commands</a>"
"</div>"

"<main>"

/* ======================================================= */
/* ── Section: Home ── */
/* ======================================================= */
"<div id='sec-home' class='section'>"

"<div id='status' class='status-connected'>Connected</div>"

/* ── Scripts ── */
"<section>"
"<h1>Scripts <a class='primary' onclick='refreshScripts()' style='cursor:pointer;float:right;font-size:1.5rem'>&#x21bb;</a></h1>"
"<table class='table' id='scriptTable'>"
"<thead><tr><th>File</th><th>Actions</th></tr></thead>"
"<tbody id='scriptList'></tbody>"
"</table>"
"<div class='row'>"
"<input type='text' id='newFile' placeholder='filename.txt'>"
"<button class='success' onclick='createScript()'>create</button>"
"</div>"
"</section>"

/* ── Editor ── */
"<section>"
"<h1>Editor"
" <a class='primary' onclick='downloadScript()' style='cursor:pointer;float:right;font-size:0.8em;margin-left:10px' title='Download'>&#x2B07; download</a>"
" <a class='primary' onclick='reloadEditor()'  style='cursor:pointer;float:right;font-size:1.5rem' title='Reload'>&#x21bb;</a>"
"</h1>"
"<div class='row'>"
"<input type='text' id='editorFile' placeholder='filename.txt' readonly style='width:100%;box-sizing:border-box'>"
"</div>"
"<div class='row'><textarea id='editor'></textarea></div>"
"<div class='row'><div class='debugger'>&#x25B8; <span id='editorinfo'>-</span></div></div>"
"<div class='row'>"
"<button class='success' onclick='saveScript()'>save</button>"
"<button class='warn'    onclick='runScript()'>run</button>"
"<button class='danger'  onclick='stopScript()'>stop</button>"
"</div>"
"</section>"

/* ── Control ── */
"<section>"
"<h1>Control <span id='stateBadge'>Idle</span></h1>"
"<div class='row'><div class='debugger' id='runInfo' style='display:none'>&#x25B8; <span id='runName'></span> &mdash; line <span id='runLine'>0</span></div></div>"
"<div class='row'>"
"<button class='success' onclick='resumeScript()'>&#9658; Start/Resume</button>"
"<button class='warn'    onclick='pauseScript()'>&#9208; Pause</button>"
"<button class='danger'  onclick='stopAll()'>&#9209; Stop</button>"
"</div>"
"</section>"

/* ── Firmware ── */
"<section>"
"<h1>Firmware Update</h1>"
"<div class='row'>"
"<input type='file' id='firmwareFile' accept='.bin'>"
"<button class='warn' onclick='uploadFirmware()'>Upload &amp; Reboot</button>"
"</div>"
"</section>"

"</div>" /* end sec-home */

/* ======================================================= */
/* ── Section: Sync ── */
/* ======================================================= */
"<div id='sec-sync' class='section'>"

"<section>"
"<h1>SD Card Sync</h1>"
"<p style='color:#888;font-size:0.9rem'>Compares scripts in internal storage (NVS) with <code>/scripts/</code> on the SD card.</p>"
"<div class='row'>"
"<button class='primary' onclick='syncPreview()'>&#x1F50D; Preview</button>"
"<button class='success' id='syncExecBtn' onclick='syncExecute()' style='display:none'>&#x2713; Execute Sync</button>"
"</div>"
"<div id='syncResult' style='margin-top:10px'></div>"
"<table class='table' id='syncTable' style='display:none'>"
"<thead><tr><th>Script</th><th>Action</th><th>NVS</th><th>SD</th><th>Conflict Resolution</th></tr></thead>"
"<tbody id='syncBody'></tbody>"
"</table>"
"</section>"

"</div>" /* end sec-sync */

/* ======================================================= */
/* ── Section: Settings ── */
/* ======================================================= */
"<div id='sec-settings' class='section'>"

"<section>"
"<h1>WiFi Settings</h1>"
"<p><i>Restart the device to apply new settings.</i></p>"
"<table class='stbl'>"
"<tr><td><b>SSID:</b></td><td id='ssid'>Loading...</td><td><button class='primary' onclick='editSSID()'>edit</button></td></tr>"
"<tr><td><b>Password:</b></td><td id='password'>Loading...</td><td><button class='primary' onclick='editPassword()'>edit</button></td></tr>"
"<tr><td><b>Channel:</b></td><td id='channel'>Loading...</td><td><button class='primary' onclick='editChannel()'>edit</button></td></tr>"
"<tr><td><b>IP Address:</b></td><td id='ip'>Loading...</td><td><button class='primary' onclick='editIP()'>edit</button></td></tr>"
"</table>"
"</section>"

"<section>"
"<h1>Auto-Run Script</h1>"
"<p>Script to run automatically on boot:</p>"
"<table class='stbl'>"
"<tr><td><b>Script:</b></td><td id='autorun'>None</td><td><button class='warn' onclick='disableAutorun()'>disable</button></td></tr>"
"</table>"
"</section>"

"<section>"
"<h1>System</h1>"
"<button class='danger' onclick='resetSettings()'>Reset All Settings</button>"
"<p><i>This will reset all settings to default values.</i></p>"
"</section>"

"</div>" /* end sec-settings */

/* ======================================================= */
/* ── Section: Commands ── */
/* ======================================================= */
"<div id='sec-commands' class='section'>"

"<h1>DuckyScript Commands</h1>"
"<p>EvilDuck32 supports the standard DuckyScript language for USB HID automation. "
"Scripts are stored in NVS flash or on the SD card (<code>/sdcard/scripts/</code>).</p>"
"<div class='note'>Commands are <b>case-insensitive</b>. "
"Lines starting with <code>REM</code> or <code>#</code> are comments. "
"A default delay of <code>100&thinsp;ms</code> is applied between commands.</div>"

"<section>"
"<h2>Basic</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Syntax</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>REM</td><td>REM [text]</td><td>Comment – ignored during execution</td><td>REM Open Run dialog</td></tr>"
"<tr><td>DELAY</td><td>DELAY &lt;ms&gt;</td><td>Wait for the given number of milliseconds</td><td>DELAY 500</td></tr>"
"<tr><td>DEFAULT_DELAY</td><td>DEFAULT_DELAY &lt;ms&gt;</td><td>Set the pause inserted after every command (0 to disable). Alias: DEFAULTDELAY</td><td>DEFAULT_DELAY 50</td></tr>"
"<tr><td>STRING</td><td>STRING &lt;text&gt;</td><td>Type a string character by character using the active keyboard locale</td><td>STRING Hello World!</td></tr>"
"<tr><td>LOCALE</td><td>LOCALE &lt;code&gt;</td><td>Switch keyboard layout. Codes: US DE GB FR ES IT PT DK BE RU SI SK CZ HU CAFR CHDE CHFR BG</td><td>LOCALE DE</td></tr>"
"<tr><td>REPEAT</td><td>REPEAT &lt;n&gt;</td><td>Repeat the last command n more times. Alias: REPLAY</td><td>REPEAT 3</td></tr>"
"<tr><td>ATTACKMODE</td><td>ATTACKMODE [mode]</td><td>Reserved for future attack-mode switching (not yet implemented)</td><td>ATTACKMODE HID</td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Modifier Keys + Combos</h2>"
"<p>Modifiers can be chained and combined with any key name. "
"Multiple modifiers are written as separate words before the key.</p>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Syntax</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>CTRL</td><td>CTRL [key]</td><td>Hold Control and optionally press a key. Alias: CONTROL</td><td>CTRL c</td></tr>"
"<tr><td>ALT</td><td>ALT [key]</td><td>Hold Alt and optionally press a key</td><td>ALT F4</td></tr>"
"<tr><td>SHIFT</td><td>SHIFT [key]</td><td>Hold Shift and optionally press a key</td><td>SHIFT TAB</td></tr>"
"<tr><td>GUI</td><td>GUI [key]</td><td>Hold Windows/Cmd and optionally press a key. Alias: WINDOWS</td><td>GUI r</td></tr>"
"<tr><td colspan=4 style='color:#ffc107;font-size:.82rem'>Multi-modifier: <code>CTRL ALT DELETE</code> &nbsp;|&nbsp; "
"<code>CTRL SHIFT ESC</code> &nbsp;|&nbsp; <code>CTRL SHIFT s</code></td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Navigation &amp; Editing Keys</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Aliases</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>ENTER</td><td>RETURN</td><td>Send Enter / Return key</td><td>ENTER</td></tr>"
"<tr><td>BACKSPACE</td><td></td><td>Send Backspace key</td><td>BACKSPACE</td></tr>"
"<tr><td>DELETE</td><td>DEL</td><td>Send Delete key</td><td>DELETE</td></tr>"
"<tr><td>INSERT</td><td></td><td>Send Insert key</td><td>INSERT</td></tr>"
"<tr><td>TAB</td><td></td><td>Send Tab key</td><td>TAB</td></tr>"
"<tr><td>SPACE</td><td></td><td>Send Space key</td><td>SPACE</td></tr>"
"<tr><td>ESC</td><td>ESCAPE</td><td>Send Escape key</td><td>ESC</td></tr>"
"<tr><td>HOME</td><td></td><td>Send Home key</td><td>HOME</td></tr>"
"<tr><td>END</td><td></td><td>Send End key</td><td>END</td></tr>"
"<tr><td>PAGEUP</td><td>PAGE_UP</td><td>Send Page Up key</td><td>PAGEUP</td></tr>"
"<tr><td>PAGEDOWN</td><td>PAGE_DOWN</td><td>Send Page Down key</td><td>PAGEDOWN</td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Arrow Keys</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Aliases</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>UP</td><td>UPARROW</td><td>Send Up Arrow key</td><td>UP</td></tr>"
"<tr><td>DOWN</td><td>DOWNARROW</td><td>Send Down Arrow key</td><td>DOWN</td></tr>"
"<tr><td>LEFT</td><td>LEFTARROW</td><td>Send Left Arrow key</td><td>LEFT</td></tr>"
"<tr><td>RIGHT</td><td>RIGHTARROW</td><td>Send Right Arrow key</td><td>RIGHT</td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Function Keys</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>F1 &ndash; F12</td><td>Send function key F1 through F12</td><td>F5</td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Lock &amp; Special Keys</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Aliases</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>CAPSLOCK</td><td></td><td>Toggle Caps Lock</td><td>CAPSLOCK</td></tr>"
"<tr><td>NUMLOCK</td><td></td><td>Toggle Num Lock</td><td>NUMLOCK</td></tr>"
"<tr><td>SCROLLLOCK</td><td></td><td>Toggle Scroll Lock</td><td>SCROLLLOCK</td></tr>"
"<tr><td>PRINTSCREEN</td><td></td><td>Send Print Screen key</td><td>PRINTSCREEN</td></tr>"
"<tr><td>PAUSE</td><td>BREAK</td><td>Send Pause / Break key</td><td>PAUSE</td></tr>"
"<tr><td>MENU</td><td>APP</td><td>Send Application / Context Menu key</td><td>MENU</td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Hardware</h2>"
"<table class='ctbl'>"
"<thead><tr><th>Command</th><th>Syntax</th><th>Description</th><th>Example</th></tr></thead>"
"<tbody>"
"<tr><td>LED</td><td>LED &lt;r&gt; &lt;g&gt; &lt;b&gt;</td><td>Set the WS2812 RGB status LED colour (0&ndash;255 per channel)</td><td>LED 255 0 0</td></tr>"
"<tr><td>KEYCODE</td><td>KEYCODE &lt;mod&gt; &lt;k1&gt; [k2..k6]</td><td>Send a raw HID keyboard report. mod = modifier byte (decimal), k1..k6 = keycodes</td><td>KEYCODE 2 4 0 0 0 0 0</td></tr>"
"</tbody></table>"
"<h2>LED Color Reference</h2>"
"<table class='ctbl' style='table-layout:fixed'>"
"<thead><tr><th style='width:12%'>Color</th><th style='width:15%'>Swatch</th><th style='width:30%'>DuckyScript</th><th>Notes</th></tr></thead>"
"<tbody>"
"<tr><td>Off</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#000;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 0 0 0</code></td><td>LED ausschalten</td></tr>"
"<tr><td>White</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#ffffff;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 255 255 255</code></td><td>Bereit / Idle</td></tr>"
"<tr><td>Blue</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#0000ff;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 0 0 255</code></td><td>Script l&auml;uft</td></tr>"
"<tr><td>Red</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#ff0000;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 255 0 0</code></td><td>Fehler</td></tr>"
"<tr><td>Purple</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#8000ff;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 128 0 255</code></td><td>WiFi aktiv</td></tr>"
"<tr><td>Magenta</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#ff00ff;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 255 0 255</code></td><td></td></tr>"
"<tr><td>Green</td>"
"<td><span style='display:inline-block;width:28px;height:18px;background:#00ff00;border:1px solid #5d5d5d;border-radius:3px;vertical-align:middle'></span></td>"
"<td><code>LED 0 255 0</code></td><td></td></tr>"
"</tbody></table>"
"</section>"

"<section>"
"<h2>Keyboard Locales</h2>"
"<p>Use <code>LOCALE &lt;code&gt;</code> to switch the keyboard layout. "
"The default locale is <b>DE</b> (German). "
"Available codes:</p>"
"<p style='font-family:monospace;background:#2f3136;padding:10px;border-radius:4px;line-height:2'>"
"US &nbsp; DE &nbsp; GB &nbsp; FR &nbsp; ES &nbsp; IT &nbsp; PT &nbsp; DK &nbsp; BE &nbsp; RU &nbsp;"
"SI &nbsp; SK &nbsp; CZ &nbsp; HU &nbsp; BG &nbsp; CAFR &nbsp; CHDE &nbsp; CHFR"
"</p>"
"</section>"

"<section>"
"<h2>Example Script</h2>"
"<pre style='background:#2f3136;padding:16px;border-radius:4px;font-family:monospace;font-size:.88rem;overflow-x:auto;white-space:pre-wrap'>"
"REM Open Run dialog on Windows and execute a command\n"
"LOCALE DE\n"
"DEFAULT_DELAY 100\n"
"DELAY 1000\n"
"GUI r\n"
"DELAY 500\n"
"STRING cmd\n"
"ENTER\n"
"DELAY 800\n"
"STRING echo EvilDuck32 was here\n"
"ENTER\n"
"DELAY 300\n"
"LED 255 0 0\n"
"DELAY 500\n"
"LED 0 255 0"
"</pre>"
"</section>"

"</div>" /* end sec-commands */

"</main>"
"<script>"
/* ── SPA navigation ── */
"function showSection(id){"
"document.querySelectorAll('.section').forEach(function(s){s.style.display='none';});"
"document.getElementById('sec-'+id).style.display='block';"
"document.querySelectorAll('.nav a').forEach(function(a){a.classList.remove('active');});"
"var lnk=document.querySelector('.nav a[data-sec=\"'+id+'\"]');"
"if(lnk)lnk.classList.add('active');"
"history.pushState({},'',(id==='home')?'/':('/'+id));"
"if(id==='home'){refreshScripts();updateStatus();}"
"else if(id==='settings'){loadSettings();}"
"}"
"window.addEventListener('popstate',function(){"
"var path=location.pathname.replace('/','').replace('/','');if(!path||path==='index.html')path='home';"
"showSection(path);"
"});"
/* ── state ── */
"var currentScript='';"
"var currentAutorun='';"
"var unsavedChanged=false;"
"var pollTimer=null;"
"var STATE=['Idle','Running','Paused','Error','Finished'];"
"function E(id){return document.getElementById(id);}"
/* ── connection LED ── */
"function setConnected(ok){"
"var l=E('connled');"
"if(ok){l.classList.remove('offline');l.title='Connected';}"
"else{l.classList.add('offline');l.title='Disconnected';}}"
/* ── adaptive polling ── */
"function schedulePoll(fast){"
"clearTimeout(pollTimer);"
"pollTimer=setTimeout(updateStatus,fast?500:5000);}"
/* ── status update ── */
"function updateStatus(){"
"fetch('/api/status').then(function(r){return r.json();}).then(function(d){"
"setConnected(true);"
"var state=STATE[d.state]||'?';"
"var badge=E('stateBadge');"
"badge.textContent=state;"
"badge.className='';"
"if(d.state===1)badge.className='running';"
"else if(d.state===2)badge.className='paused';"
"else if(d.state===3)badge.className='error';"
"var running=(d.state===1||d.state===2);"
"if(running&&d.filename&&d.filename[0]!='\\0'){"
"E('runInfo').style.display='block';"
"E('runName').textContent=d.filename;"
"E('runLine').textContent=d.current_line;"
"}else{E('runInfo').style.display='none';}"
"schedulePoll(running);"
"}).catch(function(){setConnected(false);schedulePoll(false);});}"
/* ── script list ── */
"function refreshScripts(){"
"fetch('/api/settings').then(function(r){return r.json();}).then(function(s){"
"currentAutorun=s.autorun||'';"
"return fetch('/api/scripts');"
"}).catch(function(){return fetch('/api/scripts');})"
".then(function(r){return r.json();})"
".then(function(d){"
"var list=E('scriptList');list.innerHTML='';"
"if(d.scripts&&d.scripts.length>0){"
"for(var i=0;i<d.scripts.length;i++){"
"var n=d.scripts[i];"
"var tr=document.createElement('tr');"
"if(n===currentAutorun)tr.className='autorun-row';"
"var ar=(n===currentAutorun)?'&#x2605; auto':'&#x2606; auto';"
"tr.innerHTML='<td>'+n+'</td><td nowrap>"
"<button class=\"primary\" onclick=\"loadScript(\\''+n+'\\')\"   >edit</button>"
"<button class=\"warn\"    onclick=\"runScriptByName(\\''+n+'\\')\" >run</button>"
"<button class=\"success\" onclick=\"toggleAutorun(\\''+n+'\\')\"  >'+ar+'</button>"
"<button class=\"danger\"  onclick=\"deleteScript(\\''+n+'\\')\"   >delete</button>"
"</td>';"
"list.appendChild(tr);}"
"if(!currentScript&&d.scripts.length>0)loadScript(d.scripts[0]);"
"}else{list.innerHTML='<tr><td colspan=2>No scripts found. Create one below!</td></tr>';}"
"}).catch(function(e){E('scriptList').innerHTML='<tr><td colspan=2>Error loading scripts</td></tr>';console.error(e);});}"
/* ── autorun ── */
"function toggleAutorun(name){"
"var newVal=(name===currentAutorun)?'':name;"
"fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({autorun:newVal})})"
".then(function(){currentAutorun=newVal;refreshScripts();})"
".catch(function(e){editorMsg('autorun error');console.error(e);});}"
/* ── editor helpers ── */
"function editorMsg(msg){E('editorinfo').textContent=msg;}"
"function loadScript(name){"
"currentScript=name;E('editorFile').value=name;editorMsg('loading...');"
"fetch('/api/script/'+encodeURIComponent(name)).then(function(r){return r.text();}).then(function(c){"
"E('editor').value=c;editorMsg('loaded');unsavedChanged=false;"
"}).catch(function(e){editorMsg('load error');console.error(e);});}"
"function reloadEditor(){if(currentScript)loadScript(currentScript);}"
"function saveScript(){"
"var name=E('editorFile').value.trim();"
"if(!name)return alert('No filename');"
"editorMsg('saving...');"
"fetch('/api/script/'+encodeURIComponent(name),{method:'PUT',body:E('editor').value})"
".then(function(r){if(r.ok){editorMsg('saved');unsavedChanged=false;currentScript=name;refreshScripts();}else{editorMsg('save error ('+r.status+')');}})"
".catch(function(e){editorMsg('save error');console.error(e);});}"
"function deleteScript(name){"
"if(!confirm('Delete '+name+'?'))return;"
"clearTimeout(pollTimer);"
"fetch('/api/script/'+encodeURIComponent(name),{method:'DELETE'})"
".then(function(r){"
"if(r.ok){"
"if(currentScript===name){E('editor').value='';E('editorFile').value='';editorMsg('deleted');currentScript='';}"
"refreshScripts();"
"}else{editorMsg('delete error ('+r.status+')');}"
"}).catch(function(e){editorMsg('delete error');console.error(e);});}"
"function downloadScript(){"
"var content=E('editor').value;"
"if(!content)return alert('Editor is empty');"
"var name=E('editorFile').value||'script.txt';"
"var a=document.createElement('a');"
"a.href='data:text/plain;charset=utf-8,'+encodeURIComponent(content);"
"a.download=name;a.click();}"
"function createScript(){"
"var name=E('newFile').value.trim();"
"if(!name){alert('Enter filename');return;}"
"if(!name.endsWith('.txt'))name+='.txt';"
"currentScript=name;E('editorFile').value=name;"
"E('editor').value='REM New DuckyScript\\nREM EvilDuck32\\n\\nDELAY 1000\\nSTRING Hello World\\nENTER';"
"editorMsg('new file – saving...');unsavedChanged=true;E('newFile').value='';"
"saveScript();}"
/* ── control ── */
"function runScript(){"
"if(!currentScript){if(confirm('No script loaded. Create new?'))createScript();return;}"
"if(unsavedChanged&&!confirm('Unsaved changes – save first?'))return;"
"if(unsavedChanged)saveScript();"
"runScriptByName(currentScript);}"
"function runScriptByName(name){ctrl('run',name);}"
"function stopScript(){ctrl('stop');}"
"function stopAll(){ctrl('stop');}"
"function pauseScript(){ctrl('pause');}"
"function resumeScript(){ctrl('resume');}"
"function ctrl(action,filename){"
"var body={action:action};"
"if(filename)body.filename=filename;"
"fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})"
".then(function(){schedulePoll(true);})"
".catch(function(e){console.error(e);});}"
/* ── firmware ── */
"function uploadFirmware(){"
"var file=E('firmwareFile').files[0];"
"if(!file)return alert('Select a .bin file');"
"var fd=new FormData();fd.append('file',file);"
"fetch('/api/update',{method:'POST',body:fd})"
".then(function(){alert('Uploaded. Device will reboot.');}).catch(function(e){alert('Error: '+e);});}"
/* ── settings page ── */
"function loadSettings(){"
"fetch('/api/settings').then(function(r){return r.json();}).then(function(d){"
"E('ssid').textContent=d.ssid;"
"E('password').textContent=d.password;"
"E('channel').textContent=d.channel;"
"E('ip').textContent=d.ip;"
"E('autorun').textContent=d.autorun||'None';"
"}).catch(function(e){console.error('Settings error:',e);});}"
"function saveSettings(data){"
"fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})"
".then(function(r){return r.json();})"
".then(function(d){loadSettings();alert('Settings saved! Restart to apply.');})"
".catch(function(e){alert('Error: '+e);});}"
"function editSSID(){"
"var newSSID=prompt('Enter new SSID (1-32 chars):',E('ssid').textContent);"
"if(newSSID&&newSSID.length>=1&&newSSID.length<=32)saveSettings({ssid:newSSID});"
"else if(newSSID)alert('Invalid SSID length!');}"
"function editPassword(){"
"var newPass=prompt('Enter new password (8-64 chars):','');"
"if(newPass&&newPass.length>=8&&newPass.length<=64)saveSettings({password:newPass});"
"else if(newPass)alert('Invalid password length (8-64 chars)!');}"
"function editChannel(){"
"var newCh=prompt('Enter new channel (1-13):',E('channel').textContent);"
"if(newCh){var ch=parseInt(newCh);if(ch>=1&&ch<=13)saveSettings({channel:ch});else alert('Invalid channel!');}}"
"function editIP(){"
"var newIP=prompt('Enter new IP address:',E('ip').textContent);"
"if(newIP)saveSettings({ip:newIP,gateway:newIP,netmask:'255.255.255.0'});}"
"function disableAutorun(){saveSettings({autorun:''});}"
"function resetSettings(){"
"if(confirm('Reset all settings to default?')){"
"fetch('/api/settings/reset',{method:'POST'})"
".then(function(){alert('Settings reset! Restart to apply.');loadSettings();})"
".catch(function(e){alert('Error: '+e);})}}"
/* ── init ── */
"E('editor').addEventListener('input',function(){unsavedChanged=true;editorMsg('unsaved');});"
"window.onbeforeunload=function(){if(unsavedChanged)return 'Unsaved changes will be lost.';};"
/* ── Sync ── */
"var syncData=null;"
"function syncPreview(){"
"document.getElementById('syncResult').innerHTML='<em>Loading...</em>';"
"document.getElementById('syncTable').style.display='none';"
"document.getElementById('syncExecBtn').style.display='none';"
"fetch('/api/sync/preview').then(function(r){return r.json();})"
".then(function(d){"
"syncData=d;"
"var html='';"
"var actionLabel={'export':'&#x2B06; NVS&rarr;SD','import':'&#x2B07; SD&rarr;NVS','conflict':'&#x26A0; Conflict','equal':'&#x2713; Equal'};"
"var actionColor={'export':'#4caf50','import':'#2196f3','conflict':'#ff9800','equal':'#888'};"
"for(var i=0;i<d.entries.length;i++){"
"var e=d.entries[i];"
"var resolve='';"
"if(e.action==='conflict'){"
"resolve='<select id=\"res_'+i+'\">'+"
"'<option value=\"skip\">Skip</option>'+"
"'<option value=\"nvs\">NVS wins</option>'+"
"'<option value=\"sd\">SD wins</option>'+"
"'</select>';"
"}"
"html+='<tr>'+"
"'<td>'+e.name+'</td>'+"
"'<td style=\"color:'+actionColor[e.action]+'\">'+actionLabel[e.action]+'</td>'+"
"'<td>'+(e.nvs_size?e.nvs_size+' B':'-')+'</td>'+"
"'<td>'+(e.sd_size?e.sd_size+' B':'-')+'</td>'+"
"'<td>'+resolve+'</td>'+"
"'</tr>';"
"}"
"document.getElementById('syncBody').innerHTML=html;"
"document.getElementById('syncTable').style.display='table';"
"var summary='Export: '+d.to_export+', Import: '+d.to_import+', Conflicts: '+d.conflicts;"
"document.getElementById('syncResult').innerHTML='<span style=\"color:#888\">'+summary+'</span>';"
"if(d.to_export+d.to_import+d.conflicts>0)document.getElementById('syncExecBtn').style.display='';"
"})"
".catch(function(e){document.getElementById('syncResult').innerHTML='<span style=color:red>Error: '+e+'</span>';});}"

"function syncExecute(){"
"if(!syncData){alert('Run Preview first.');return;}"
"var resolutions=[];"
"for(var i=0;i<syncData.entries.length;i++){"
"var e=syncData.entries[i];"
"if(e.action==='conflict'){"
"var sel=document.getElementById('res_'+i);"
"resolutions.push({name:e.name,resolve:sel?sel.value:'skip'});"
"}}"
"fetch('/api/sync/execute',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({resolutions:resolutions})})"
".then(function(r){return r.json();})"
".then(function(d){"
"document.getElementById('syncResult').innerHTML='<span style=\"color:#4caf50\">&#x2713; Done: '+d.synced+' synced, '+d.skipped+' skipped</span>';"
"document.getElementById('syncExecBtn').style.display='none';"
"syncData=null;"
"})"
".catch(function(e){alert('Sync error: '+e);});}"

/* Show correct section on initial load */
"(function(){"
"var path=location.pathname.replace(/^\\/+/,'');"
"if(!path||path==='index.html')path='home';"
"var valid=['home','sync','settings','commands'];"
"if(valid.indexOf(path)===-1)path='home';"
"showSection(path);"
"})();"
"</script></body></html>";

// ===== Simple JSON builder ===== //
static char json_buf[1024];
static int json_pos = 0;

static void json_start(void) {
    memset(json_buf, 0, sizeof(json_buf));
    json_pos = 0;
}

static void json_add_str(const char *key, const char *value) {
    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                         "\"%s\":\"%s\",", key, value);
}

static void json_add_bool(const char *key, bool value) {
    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                         "\"%s\":%s,", key, value ? "true" : "false");
}

static void json_add_int(const char *key, int value) {
    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                         "\"%s\":%d,", key, value);
}

static void json_start_obj(const char *key) {
    if (key) {
        json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                             "\"%s\":{", key);
    } else {
        json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos, "{");
    }
}

static void json_end_obj(void) {
    if (json_pos > 0 && json_buf[json_pos - 1] == ',') {
        json_pos--;
    }
    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos, "},");
}

static char* json_finish(void) {
    if (json_pos > 0 && json_buf[json_pos - 1] == ',') {
        json_pos--;
    }
    json_buf[json_pos] = '}';
    json_buf[json_pos + 1] = '\0';
    return json_buf;
}

// ===== Simple JSON parser ===== //
static const char* json_get_str(const char *json, const char *key) {
    static char value[128];
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start) return NULL;
    start += strlen(search);

    const char *end = strchr(start, '"');
    if (!end) return NULL;

    int len = end - start;
    if (len >= sizeof(value)) len = sizeof(value) - 1;
    memcpy(value, start, len);
    value[len] = '\0';

    return value;
}

// ===== Send helpers ===== //

// Send all bytes – handles partial sends (send() may not transmit everything at once)
static int send_all(int sock, const void *data, int len) {
    const char *p = (const char *)data;
    int remaining = len;
    while (remaining > 0) {
        int sent = send(sock, p, remaining, 0);
        if (sent <= 0) return sent;
        p += sent;
        remaining -= sent;
    }
    return len;
}

// ===== HTTP Response Helpers ===== //
static void send_response(int sock, const char *status, const char *content_type, const char *body) {
    char header[512];
    int body_len = body ? strlen(body) : 0;
    snprintf(header, sizeof(header),
             "%s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n\r\n",
             status, content_type, body_len);

    send_all(sock, header, strlen(header));
    if (body) {
        send_all(sock, body, body_len);
    }
}

// ===== API Handlers ===== //
static void handle_api_status(int sock) {
    ducky_script_info_t info;
    ducky_get_info(&info);

    json_start();
    json_start_obj(NULL);
    json_add_str("mode", "HID");
    json_add_bool("sd", sdcard_is_mounted());
    json_add_bool("wifi", wifi_manager_is_started());
    if (wifi_manager_is_started()) {
        json_add_str("ip", wifi_manager_get_ip());
    } else {
        json_add_str("ip", "");
    }
    json_add_bool("hid", usb_hid_is_mounted());
    json_add_int("state", info.state);
    if (info.filename[0] != '\0') {
        json_add_str("filename", info.filename);
        json_add_int("current_line", info.current_line);
        json_add_int("total_lines", info.total_lines);
    }
    char *response = json_finish();

    ESP_LOGI(TAG, "Status response: %s", response);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", response);
}

static void handle_api_scripts(int sock) {
    // Get script names from NVS storage
    char names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int count = script_storage_list(names, MAX_SCRIPTS);

    ESP_LOGI(TAG, "Found %d scripts in NVS", count);

    // Build JSON: {"scripts": ["file1.txt", "file2.txt"]}
    memset(json_buf, 0, sizeof(json_buf));
    json_pos = 0;
    json_pos += snprintf(json_buf, sizeof(json_buf), "{\"scripts\":[");

    for (int i = 0; i < count; i++) {
        json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                             "\"%s\"%s", names[i], (i < count - 1) ? "," : "");
    }

    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos, "]}");

    ESP_LOGI(TAG, "Scripts response: %s", json_buf);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", json_buf);
}

static void handle_api_debug(int sock) {
    // Debug endpoint to check NVS status
    char names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int count = script_storage_list(names, MAX_SCRIPTS);

    // Build debug response - ensure buffer is properly initialized
    memset(json_buf, 0, sizeof(json_buf));
    json_pos = 0;
    json_pos += snprintf(json_buf, sizeof(json_buf),
        "{\"debug\":{"
        "\"nvs_count\":%d,"
        "\"nvs_partition\":\"%s\","
        "\"max_scripts\":%d,"
        "\"scripts\":[",
        count, NVS_SCRIPT_NS, MAX_SCRIPTS);

    for (int i = 0; i < count; i++) {
        json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                             "\"%s\"%s", names[i], (i < count - 1) ? "," : "");
    }

    json_pos += snprintf(json_buf + json_pos, sizeof(json_buf) - json_pos,
                         "]}}");

    send_response(sock, "HTTP/1.1 200 OK", "application/json", json_buf);
}

static void handle_api_control(int sock, const char *body) {
    // json_get_str uses a static buffer – copy action before the second call overwrites it
    char action[32] = {0};
    const char *action_ptr = json_get_str(body, "action");
    if (action_ptr) strncpy(action, action_ptr, sizeof(action) - 1);

    const char *filename = json_get_str(body, "filename");

    esp_err_t ret = ESP_OK;
    if (action[0] == '\0') {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No action specified\"}");
        return;
    }

    if (strcmp(action, "run") == 0 && filename) {
        ret = ducky_run_script(filename);
    } else if (strcmp(action, "stop") == 0) {
        ret = ducky_stop();
    } else if (strcmp(action, "pause") == 0) {
        ret = ducky_pause();
    } else if (strcmp(action, "resume") == 0) {
        ret = ducky_resume();
    } else {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"Unknown action\"}");
        return;
    }

    if (ret == ESP_OK) {
        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"ok\"}");
    } else {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}", esp_err_to_name(ret));
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json", err_buf);
    }
}

static void handle_api_script_get(int sock, const char *filename) {
    ESP_LOGI(TAG, "Loading script from NVS: %s", filename);

    char buffer[MAX_SCRIPT_SIZE + 1];
    esp_err_t ret = script_storage_load(filename, buffer, sizeof(buffer));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Script loaded successfully: %s (%d bytes)", filename, strlen(buffer));
        send_response(sock, "HTTP/1.1 200 OK", "text/plain", buffer);
    } else {
        ESP_LOGE(TAG, "Failed to load script: %s", filename);
        send_response(sock, "HTTP/1.1 404 Not Found", "application/json", "{\"error\":\"Script not found\"}");
    }
}

static void handle_api_script_put(int sock, const char *filename, const char *body, int body_len) {
    ESP_LOGI(TAG, "Saving script to NVS: %s (%d bytes)", filename, body_len);

    // Create null-terminated content buffer
    char content[MAX_SCRIPT_SIZE + 1];
    int copy_len = (body_len < MAX_SCRIPT_SIZE) ? body_len : MAX_SCRIPT_SIZE;
    memcpy(content, body, copy_len);
    content[copy_len] = '\0';

    if (body_len > 0) {
        ESP_LOGI(TAG, "Content preview: %.50s...", content);
    }

    esp_err_t ret = script_storage_save(filename, content);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Script saved successfully: %s", filename);
        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"saved\"}");
    } else {
        ESP_LOGE(TAG, "Failed to save script: %s - error: %s", filename, esp_err_to_name(ret));
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json", "{\"error\":\"Failed to save\"}");
    }
}

static void handle_api_script_delete(int sock, const char *filename) {
    ESP_LOGI(TAG, "Deleting script from NVS: %s", filename);

    esp_err_t ret = script_storage_delete(filename);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Script deleted successfully: %s", filename);
        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"deleted\"}");
    } else {
        ESP_LOGE(TAG, "Failed to delete script: %s - error: %s", filename, esp_err_to_name(ret));
        send_response(sock, "HTTP/1.1 404 Not Found", "application/json", "{\"error\":\"Script not found\"}");
    }
}

static void handle_api_upload(int sock, const char *body, int body_len) {
    // Parse multipart/form-data to extract filename and content
    // Simple parser for: Content-Disposition: form-data; name="file"; filename="test.txt"

    const char *filename_start = strstr(body, "filename=\"");
    if (!filename_start) {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No filename\"}");
        return;
    }
    filename_start += 10; // Skip filename="

    const char *filename_end = strchr(filename_start, '\"');
    if (!filename_end) {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"Invalid filename\"}");
        return;
    }

    char filename[64];
    int filename_len = filename_end - filename_start;
    if (filename_len >= sizeof(filename)) filename_len = sizeof(filename) - 1;
    memcpy(filename, filename_start, filename_len);
    filename[filename_len] = '\0';

    // Find content start (after \r\n\r\n following the headers)
    const char *headers_end = strstr(body, "\r\n\r\n");
    if (!headers_end) {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No content\"}");
        return;
    }
    const char *content = headers_end + 4;
    int content_len = body_len - (content - body);

    // Remove trailing boundary if present
    const char *boundary_end = strstr(content, "\r\n--");
    if (boundary_end) {
        content_len = boundary_end - content;
    }

    // Build path and save to scripts directory
    char path[128];
    snprintf(path, sizeof(path), "scripts/%s", filename);

    esp_err_t ret = sdcard_write_file(path, content, content_len);

    if (ret == ESP_OK) {
        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"uploaded\"}");
    } else {
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json", "{\"error\":\"Upload failed\"}");
    }
}

// GET /payload/<filename>  – serve a file from /sdcard/payloads/ to the target
// Used by DuckyScript payloads to deliver files via PowerShell iwr/WebClient
#define PAYLOAD_SEND_BUF 512
static void handle_payload_download(int sock, const char *filename)
{
    // Reject path traversal attempts
    if (strchr(filename, '/') || strstr(filename, "..")) {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"Invalid filename\"}");
        return;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/payloads/%s", filename);

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Payload not found: %s", filepath);
        send_response(sock, "HTTP/1.1 404 Not Found", "application/json", "{\"error\":\"Payload not found\"}");
        return;
    }

    // Determine file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Determine Content-Type by extension
    const char *content_type = "application/octet-stream";
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".ps1") == 0 || strcmp(ext, ".txt") == 0) content_type = "text/plain";
        else if (strcmp(ext, ".bat") == 0 || strcmp(ext, ".cmd") == 0) content_type = "text/plain";
        else if (strcmp(ext, ".exe") == 0) content_type = "application/octet-stream";
    }

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Content-Disposition: attachment; filename=\"%s\"\r\n"
             "Connection: close\r\n\r\n",
             content_type, file_size, filename);
    send_all(sock, header, strlen(header));

    uint8_t buf[PAYLOAD_SEND_BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (send_all(sock, buf, (int)n) <= 0) break;
    }

    fclose(f);
    ESP_LOGI(TAG, "Payload served: %s (%ld bytes)", filename, file_size);
}

// POST /loot/upload  – save exfiltrated file to /sdcard/loot/<filename>
// Supports two modes:
//   a) multipart/form-data  (same format as /api/upload)
//   b) raw body + "filename" HTTP header (used by DuckyScript payloads)
static void handle_loot_upload(int sock, const char *headers_raw,
                                const char *body, int body_len)
{
    char filename[64] = {0};

    // Mode A: multipart – look for Content-Disposition filename field
    const char *fn_field = strstr(body, "filename=\"");
    if (fn_field) {
        fn_field += 10;
        const char *fn_end = strchr(fn_field, '"');
        if (fn_end) {
            int fn_len = fn_end - fn_field;
            if (fn_len >= (int)sizeof(filename)) fn_len = sizeof(filename) - 1;
            memcpy(filename, fn_field, fn_len);
            filename[fn_len] = '\0';
        }
        // Advance body past the part headers
        const char *part_body = strstr(body, "\r\n\r\n");
        if (part_body) {
            part_body += 4;
            int part_len = body_len - (part_body - body);
            const char *trail = strstr(part_body, "\r\n--");
            if (trail) part_len = trail - part_body;
            body     = part_body;
            body_len = part_len;
        }
    }

    // Mode B: plain "filename" header
    if (filename[0] == '\0' && headers_raw) {
        const char *hdr = strcasestr(headers_raw, "\r\nfilename:");
        if (hdr) {
            hdr += 11; // skip "\r\nfilename:"
            while (*hdr == ' ') hdr++;
            const char *eol = strstr(hdr, "\r\n");
            int fn_len = eol ? (int)(eol - hdr) : (int)strlen(hdr);
            if (fn_len >= (int)sizeof(filename)) fn_len = sizeof(filename) - 1;
            memcpy(filename, hdr, fn_len);
            filename[fn_len] = '\0';
        }
    }

    if (filename[0] == '\0') {
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json",
                      "{\"error\":\"No filename\"}");
        return;
    }

    // Sanitize: no path separators
    for (char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') *p = '_';
    }

    char path[96];
    snprintf(path, sizeof(path), "loot/%s", filename);
    esp_err_t ret = sdcard_write_file(path, body, body_len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loot saved: %s (%d bytes)", path, body_len);
        send_response(sock, "HTTP/1.1 200 OK", "application/json",
                      "{\"status\":\"saved\"}");
    } else {
        ESP_LOGE(TAG, "Loot save failed: %s", esp_err_to_name(ret));
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json",
                      "{\"error\":\"Save failed\"}");
    }
}

// ===== Settings API Handlers ===== //
static void handle_api_settings_get(int sock) {
    char json_buf[512];
    settings_to_json(json_buf, sizeof(json_buf));

    ESP_LOGI(TAG, "Settings response: %s", json_buf);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", json_buf);
}

static void handle_api_settings_set(int sock, const char *body) {
    ESP_LOGI(TAG, "Settings set request: %s", body);

    esp_err_t ret = settings_parse_json(body);
    if (ret == ESP_OK) {
        // Save to NVS
        settings_save();

        char json_buf[512];
        settings_to_json(json_buf, sizeof(json_buf));
        ESP_LOGI(TAG, "Settings saved: %s", json_buf);

        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"saved\"}");
    } else {
        ESP_LOGE(TAG, "Settings parse error: %s", esp_err_to_name(ret));
        send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"Invalid JSON\"}");
    }
}

static void handle_api_settings_reset(int sock) {
    ESP_LOGI(TAG, "Settings reset request");

    esp_err_t ret = settings_reset();
    if (ret == ESP_OK) {
        send_response(sock, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"reset\"}");
    } else {
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json", "{\"error\":\"Reset failed\"}");
    }
}

// ===== Backup/Restore API Handlers ===== //
static void handle_api_backup(int sock) {
    ESP_LOGI(TAG, "Backup request: NVS -> SD-Card");

    // Check if SD card is mounted
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted for backup");
        send_response(sock, "HTTP/1.1 503 Service Unavailable", "application/json", "{\"error\":\"SD card not mounted\"}");
        return;
    }

    // Get all script names from NVS
    char names[MAX_SCRIPTS][MAX_SCRIPT_NAME];
    int count = script_storage_list(names, MAX_SCRIPTS);

    ESP_LOGI(TAG, "Backing up %d scripts from NVS to SD-Card", count);

    int backed_up = 0;
    int failed = 0;

    for (int i = 0; i < count; i++) {
        // Load script from NVS
        char content[MAX_SCRIPT_SIZE + 1];
        esp_err_t ret = script_storage_load(names[i], content, sizeof(content));

        if (ret == ESP_OK) {
            // Write to SD card
            char path[128];
            snprintf(path, sizeof(path), "scripts/%s", names[i]);

            ret = sdcard_write_file(path, content, strlen(content));
            if (ret == ESP_OK) {
                backed_up++;
                ESP_LOGI(TAG, "Backed up: %s", names[i]);
            } else {
                failed++;
                ESP_LOGE(TAG, "Failed to backup %s: %s", names[i], esp_err_to_name(ret));
            }
        } else {
            failed++;
            ESP_LOGE(TAG, "Failed to load %s from NVS: %s", names[i], esp_err_to_name(ret));
        }
    }

    // Build response
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"backup_completed\",\"total\":%d,\"backed_up\":%d,\"failed\":%d}",
             count, backed_up, failed);

    ESP_LOGI(TAG, "Backup completed: %d/%d successful", backed_up, count);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", response);
}

static void handle_api_restore(int sock) {
    ESP_LOGI(TAG, "Restore request: SD-Card -> NVS");

    // Check if SD card is mounted
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted for restore");
        send_response(sock, "HTTP/1.1 503 Service Unavailable", "application/json", "{\"error\":\"SD card not mounted\"}");
        return;
    }

    // Get all script names from /sdcard/scripts/
    char payloads[MAX_SCRIPTS][64];
    int count = sdcard_list_scripts(payloads, MAX_SCRIPTS);

    ESP_LOGI(TAG, "Restoring %d scripts from SD-Card /scripts/ to NVS", count);

    int restored = 0;
    int failed = 0;

    for (int i = 0; i < count; i++) {
        // Read script from SD card
        char content[MAX_SCRIPT_SIZE + 1];
        size_t bytes_read = 0;

        char script_path[96];
        snprintf(script_path, sizeof(script_path), "scripts/%s", payloads[i]);
        esp_err_t ret = sdcard_read_file(script_path, content, sizeof(content), &bytes_read);
        if (ret == ESP_OK) {
            content[bytes_read] = '\0';

            // Save to NVS
            ret = script_storage_save(payloads[i], content);
            if (ret == ESP_OK) {
                restored++;
                ESP_LOGI(TAG, "Restored: %s (%d bytes)", payloads[i], bytes_read);
            } else {
                failed++;
                ESP_LOGE(TAG, "Failed to save %s to NVS: %s", payloads[i], esp_err_to_name(ret));
            }
        } else {
            failed++;
            ESP_LOGE(TAG, "Failed to read %s from SD: %s", payloads[i], esp_err_to_name(ret));
        }
    }

    // Build response
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"restore_completed\",\"total\":%d,\"restored\":%d,\"failed\":%d}",
             count, restored, failed);

    ESP_LOGI(TAG, "Restore completed: %d/%d successful", restored, count);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", response);
}

// ===== Sync API Handlers ===== //

static void handle_api_sync_preview(int sock) {
    if (!sdcard_is_mounted()) {
        send_response(sock, "HTTP/1.1 503 Service Unavailable", "application/json",
                      "{\"error\":\"SD card not mounted\"}");
        return;
    }

    static sync_preview_t preview;
    esp_err_t ret = script_storage_sync_preview(&preview);
    if (ret != ESP_OK) {
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json",
                      "{\"error\":\"Preview failed\"}");
        return;
    }

    static char resp[2048];
    int pos = 0;
    pos += snprintf(resp + pos, sizeof(resp) - pos,
                    "{\"to_export\":%d,\"to_import\":%d,\"conflicts\":%d,\"entries\":[",
                    preview.to_export, preview.to_import, preview.conflicts);

    for (int i = 0; i < preview.count && pos < (int)sizeof(resp) - 80; i++) {
        const sync_entry_t *e = &preview.entries[i];
        const char *action_str;
        switch (e->action) {
            case SYNC_ACTION_EXPORT:   action_str = "export";   break;
            case SYNC_ACTION_IMPORT:   action_str = "import";   break;
            case SYNC_ACTION_CONFLICT: action_str = "conflict"; break;
            default:                   action_str = "equal";    break;
        }
        pos += snprintf(resp + pos, sizeof(resp) - pos,
                        "%s{\"name\":\"%s\",\"action\":\"%s\","
                        "\"nvs_size\":%d,\"sd_size\":%d}",
                        i > 0 ? "," : "",
                        e->name, action_str,
                        (int)e->nvs_size, (int)e->sd_size);
    }
    pos += snprintf(resp + pos, sizeof(resp) - pos, "]}");

    send_response(sock, "HTTP/1.1 200 OK", "application/json", resp);
}

static void handle_api_sync_execute(int sock, const char *body) {
    if (!sdcard_is_mounted()) {
        send_response(sock, "HTTP/1.1 503 Service Unavailable", "application/json",
                      "{\"error\":\"SD card not mounted\"}");
        return;
    }

    // Build preview first
    static sync_preview_t preview;
    if (script_storage_sync_preview(&preview) != ESP_OK) {
        send_response(sock, "HTTP/1.1 500 Internal Server Error", "application/json",
                      "{\"error\":\"Preview failed\"}");
        return;
    }

    // Parse conflict resolutions from JSON body.
    // Expected format: {"resolutions":[{"name":"foo.txt","resolve":"nvs"},{"name":"bar.txt","resolve":"sd"}]}
    // resolve values: "nvs" = NVS wins, "sd" = SD wins, "skip" (or absent) = skip
    static conflict_resolution_t resolutions[MAX_SCRIPTS];
    int res_count = 0;

    if (body) {
        const char *p = body;
        while ((p = strstr(p, "\"name\":\"")) != NULL && res_count < MAX_SCRIPTS) {
            p += 8; // skip "name":"
            const char *name_end = strchr(p, '"');
            if (!name_end) break;

            strncpy(resolutions[res_count].name, p, name_end - p);
            resolutions[res_count].name[name_end - p] = '\0';

            // Find resolve field after this name
            const char *r = strstr(name_end, "\"resolve\":\"");
            if (r) {
                r += 11;
                if (strncmp(r, "nvs", 3) == 0) {
                    resolutions[res_count].resolve = CONFLICT_NVS_WINS;
                } else if (strncmp(r, "sd", 2) == 0) {
                    resolutions[res_count].resolve = CONFLICT_SD_WINS;
                } else {
                    resolutions[res_count].resolve = CONFLICT_SKIP;
                }
            } else {
                resolutions[res_count].resolve = CONFLICT_SKIP;
            }
            res_count++;
            p = name_end + 1;
        }
    }

    int synced = 0, skipped = 0;
    script_storage_sync_execute(&preview, resolutions, res_count, &synced, &skipped);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"synced\":%d,\"skipped\":%d}", synced, skipped);
    send_response(sock, "HTTP/1.1 200 OK", "application/json", resp);
}

// ===== HTTP Request Handler ===== //
static void handle_client(int sock) {
    // Static buffer: prevents stack overflow (task stack is only 8 KB).
    // Safe because the server handles one connection at a time (single-threaded).
    static char buffer[16384];
    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(sock);
        return;
    }
    buffer[len] = '\0';

    // If there is a body, ensure we receive it completely (Content-Length based)
    char *hdr_end = strstr(buffer, "\r\n\r\n");
    if (hdr_end) {
        int body_offset = (int)(hdr_end + 4 - buffer);
        int body_received = len - body_offset;

        // Parse Content-Length header (case-sensitive, standard casing)
        int content_length = 0;
        const char *cl = strstr(buffer, "\r\nContent-Length: ");
        if (cl) {
            content_length = atoi(cl + 18); // 18 = strlen("\r\nContent-Length: ")
        }

        // Loop recv() until we have all body bytes
        while (body_received < content_length && len < (int)(sizeof(buffer) - 1)) {
            int to_read = content_length - body_received;
            int space   = (int)(sizeof(buffer) - 1 - len);
            if (to_read > space) to_read = space;
            int extra = recv(sock, buffer + len, to_read, 0);
            if (extra <= 0) break;
            len += extra;
            buffer[len] = '\0';
            body_received += extra;
        }
    }

    ESP_LOGD(TAG, "Request: %.80s", buffer); // Debug only, truncated

    // Parse HTTP request line more carefully
    char method[16], path[256], version[16];
    int parsed = sscanf(buffer, "%15s %255s %15s", method, path, version);
    if (parsed < 2) {
        ESP_LOGE(TAG, "Failed to parse request: %s", buffer);
        send_response(sock, "HTTP/1.1 400 Bad Request", "text/html", "<h1>400 Bad Request</h1>");
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "Parsed: method='%s', path='%s'", method, path);

    // Route handling
    // All SPA pages serve the same spa_html. Every known section path is listed here
    // so the browser can load the correct section directly (e.g. after history.pushState).
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0 ||
        strcmp(path, "/settings") == 0 || strcmp(path, "/commands") == 0 ||
        strcmp(path, "/sync") == 0 ||
        strcmp(path, "/settings.html") == 0 || strcmp(path, "/commands.html") == 0) {
        send_all(sock, spa_html, strlen(spa_html));
    }
    else if (strcmp(path, "/api/status") == 0) {
        handle_api_status(sock);
    }
    else if (strcmp(path, "/api/scripts") == 0) {
        handle_api_scripts(sock);
    }
    else if (strcmp(path, "/api/debug") == 0) {
        handle_api_debug(sock);
    }
    else if (strncmp(path, "/api/script/", 12) == 0) {
        // Extract filename from path
        const char *filename = path + 12;
        if (strlen(filename) == 0) {
            send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No filename\"}");
        } else if (strcmp(method, "GET") == 0) {
            handle_api_script_get(sock, filename);
        } else if (strcmp(method, "PUT") == 0) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                int body_len = len - (body + 4 - buffer);
                handle_api_script_put(sock, filename, body + 4, body_len);
            } else {
                send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No body\"}");
            }
        } else if (strcmp(method, "DELETE") == 0) {
            handle_api_script_delete(sock, filename);
        } else {
            send_response(sock, "HTTP/1.1 405 Method Not Allowed", "application/json", "{\"error\":\"Method not allowed\"}");
        }
    }
    else if (strcmp(path, "/api/control") == 0 && strcmp(method, "POST") == 0) {
        // Find body
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            handle_api_control(sock, body + 4);
        } else {
            send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No body\"}");
        }
    }
    else if (strcmp(path, "/api/settings") == 0) {
        if (strcmp(method, "GET") == 0) {
            handle_api_settings_get(sock);
        } else if (strcmp(method, "POST") == 0) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                handle_api_settings_set(sock, body + 4);
            } else {
                send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No body\"}");
            }
        } else {
            send_response(sock, "HTTP/1.1 405 Method Not Allowed", "application/json", "{\"error\":\"Method not allowed\"}");
        }
    }
    else if (strcmp(path, "/api/settings/reset") == 0 && strcmp(method, "POST") == 0) {
        handle_api_settings_reset(sock);
    }
    else if (strcmp(path, "/api/backup") == 0 && strcmp(method, "POST") == 0) {
        handle_api_backup(sock);
    }
    else if (strcmp(path, "/api/restore") == 0 && strcmp(method, "POST") == 0) {
        handle_api_restore(sock);
    }
    else if (strcmp(path, "/api/sync/preview") == 0 && strcmp(method, "GET") == 0) {
        handle_api_sync_preview(sock);
    }
    else if (strcmp(path, "/api/sync/execute") == 0 && strcmp(method, "POST") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        handle_api_sync_execute(sock, body ? body + 4 : NULL);
    }
    else if (strcmp(path, "/api/upload") == 0 && strcmp(method, "POST") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            int body_len = len - (body + 4 - buffer);
            handle_api_upload(sock, body + 4, body_len);
        } else {
            send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No body\"}");
        }
    }
    else if (strcmp(path, "/loot/upload") == 0 && strcmp(method, "POST") == 0) {
        // Raw headers (before body) for Mode-B filename header
        char *body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) {
            int body_len = len - (body_start + 4 - buffer);
            handle_loot_upload(sock, buffer, body_start + 4, body_len);
        } else {
            send_response(sock, "HTTP/1.1 400 Bad Request", "application/json", "{\"error\":\"No body\"}");
        }
    }
    else if (strncmp(path, "/payload/", 9) == 0 && strcmp(method, "GET") == 0) {
        handle_payload_download(sock, path + 9);
    }
    else if (strcmp(path, "/api/update") == 0) {
        // TODO: Handle OTA update
        send_response(sock, "HTTP/1.1 501 Not Implemented", "application/json", "{\"error\":\"Not implemented\"}");
    }
    else {
        send_response(sock, "HTTP/1.1 404 Not Found", "text/html", "<h1>404 Not Found</h1>");
    }

    close(sock);
}

// ===== HTTP Server Task ===== //
static void http_server_task(void *arg) {
    (void)arg;

    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %s", strerror(errno));
        close(server_socket);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_socket, 5) < 0) {
        ESP_LOGE(TAG, "Failed to listen on socket: %s", strerror(errno));
        close(server_socket);
        vTaskDelete(NULL);
        return;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);

    server_running = true;
    ESP_LOGI(TAG, "HTTP server listening on port 80");

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);

        if (client_socket >= 0) {
            // Timeouts on the client socket (not server socket!)
            struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            handle_client(client_socket);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "accept error: %s", strerror(errno));
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms yield to allow other tasks to run
    }

    close(server_socket);
    server_socket = -1;
    ESP_LOGI(TAG, "HTTP server stopped");
    vTaskDelete(NULL);
}

// ===== Public API ===== //
esp_err_t webserver_init(void) {
    xTaskCreate(http_server_task, "http_server", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "HTTP server task created");
    return ESP_OK;
}

esp_err_t webserver_stop(void) {
    server_running = false;
    if (server_socket >= 0) {
        shutdown(server_socket, SHUT_RDWR);
    }
    return ESP_OK;
}

bool webserver_is_running(void) {
    return server_running;
}
