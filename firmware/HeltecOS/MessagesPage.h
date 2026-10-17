#pragma once
#include <Arduino.h>

// No external assets: the phone and board only need access to the same local network.
inline const char MESSAGE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Heltec OS Messages</title>
<style>
:root{color-scheme:dark}body{font:17px system-ui;max-width:38rem;margin:2rem auto;padding:0 1rem;background:#101820;color:#fff}
h1{font-size:1.7rem}textarea,button{box-sizing:border-box;width:100%;font:inherit;padding:12px;border-radius:8px;margin:8px 0}
textarea{background:#1b2933;color:#fff;border:1px solid #68808f}button{background:#80d8c0;color:#101820;border:0;cursor:pointer}
button:disabled{opacity:.5}article{background:#1b2933;padding:12px;margin:10px 0;border-radius:8px}article p{white-space:pre-wrap;overflow-wrap:anywhere;margin-bottom:0}
small{color:#b9cad4}#status{min-height:1.5em}a{color:#80d8c0}
</style>
<h1>Heltec OS Messages</h1><p>Phone &harr; board &middot; Morse light</p>
<p><small>Same WiFi network. Last 12 messages, kept until restart. Each sent message also flashes on the board LED.</small></p>
<form id="compose"><label for="text">Message (A-Z, 0-9 and spaces)</label>
<textarea id="text" maxlength="80" rows="3" required placeholder="HELLO HELTEC" spellcheck="false"></textarea>
<button id="send">Send to board + light</button></form>
<button id="stop" type="button">Stop light</button><p id="status" role="status" aria-live="polite">Connecting...</p>
<h2>Conversation</h2><div id="history"></div>
<details><summary>Write Morse on the board</summary><p>Open Messages &gt; Compose. Release PRG before 350 ms for a dot; hold 350-1799 ms for a dash. Pause 1.2 seconds to finish a letter. Hold at least 1.8 seconds for actions: space, send, delete, clear or back.</p>
<p>A .- &nbsp; B -... &nbsp; C -.-. &nbsp; D -.. &nbsp; E . &nbsp; F ..-. &nbsp; G --. &nbsp; H .... &nbsp; I .. &nbsp; J .--- &nbsp; K -.- &nbsp; L .-.. &nbsp; M -- &nbsp; N -. &nbsp; O --- &nbsp; P .--. &nbsp; Q --.- &nbsp; R .-. &nbsp; S ... &nbsp; T - &nbsp; U ..- &nbsp; V ...- &nbsp; W .-- &nbsp; X -..- &nbsp; Y -.-- &nbsp; Z --..</p></details>
<script nonce="{{NONCE}}">
const token='{{TOKEN}}';
const status=document.getElementById('status'), input=document.getElementById('text'), sendButton=document.getElementById('send');
let sending=false, notice='', noticeUntil=0;
async function request(path,options={}) {
 const r=await fetch(path,{cache:'no-store',...options});
 if(r.status===401){location.href='/messages';throw Error('Sign in again');}
 if(!r.ok)throw Error(await r.text());
 return r;
}
async function refresh(){
 try{
  const data=await (await request('/api/messages')).json();
  sendButton.disabled=sending||data.busy;
  const history=document.getElementById('history');history.replaceChildren();
  for(const message of data.messages){
   const article=document.createElement('article'),label=document.createElement('small'),body=document.createElement('p');
   label.textContent='#'+message.id+' · '+(message.phone?'Phone → board':'Board → phone');body.textContent=message.text;
   article.append(label,body);history.append(article);
  }
  if(!data.messages.length)history.textContent='No messages yet.';
  status.textContent=Date.now()<noticeUntil?notice:(data.busy?'Busy: light or system update in progress.':'Connected. Ready to send.');
 }catch(e){status.textContent='Connection: '+e.message;sendButton.disabled=true;}
}
document.getElementById('compose').addEventListener('submit',async e=>{
 e.preventDefault();if(sending)return;
 if(!/^[A-Za-z0-9 ]{1,80}$/.test(input.value)||!input.value.trim()){status.textContent='Use 1-80 characters: A-Z, 0-9 and spaces.';return;}
 sending=true;sendButton.disabled=true;
 try{await request('/api/messages',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Heltec-Token':token},body:new URLSearchParams({text:input.value})});input.value='';notice='Delivered to board. Emitting Morse light.';}
 catch(e){notice=e.message;}
 finally{noticeUntil=Date.now()+5000;sending=false;await refresh();}
});
document.getElementById('stop').addEventListener('click',async()=>{
 try{await request('/api/light/stop',{method:'POST',headers:{'X-Heltec-Token':token}});notice='Light stopped. Message remains in history.';}catch(e){notice=e.message;}
 noticeUntil=Date.now()+5000;await refresh();
});
async function poll(){await refresh();setTimeout(poll,1500);}poll();
</script></html>)HTML";
