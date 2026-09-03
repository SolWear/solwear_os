import { layout, solwear } from "@solwear/sdk";
const hours=document.querySelector<HTMLElement>("#hours")!;const minutes=document.querySelector<HTMLElement>("#minutes")!;const seconds=document.querySelector<HTMLElement>("#seconds")!;const date=document.querySelector<HTMLElement>("#date")!;const battery=document.querySelector<HTMLElement>("#battery")!;
const pad=(n:number)=>String(n).padStart(2,"0");
function paint(epochMs=Date.now()){const now=new Date(epochMs);hours.textContent=pad(now.getHours());minutes.textContent=pad(now.getMinutes());seconds.textContent=pad(now.getSeconds());date.textContent=now.toLocaleDateString(undefined,{weekday:"short",month:"short",day:"numeric"}).toUpperCase();}
async function start(){await solwear.ready();layout(solwear.system.screen);paint();solwear.on("tick",(tick)=>paint(tick.epochMs));const refresh=async()=>{try{const value=await solwear.power.status();battery.textContent=`${value.percent}%${value.charging?" ⚡":""}`;}catch{battery.textContent="--%";}};await refresh();setInterval(refresh,60_000);}
void start();
