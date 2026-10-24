#!/usr/bin/env python3
"""Render standalone ad previews; native Resolve titles are built by import.lua."""
from pathlib import Path
import json,subprocess,concurrent.futures
from PIL import Image,ImageDraw,ImageFont
base=Path(__file__).resolve().parents[1]/'documents/ads/color-kit-grande-30s'
m=json.loads((base/'edit-manifest.json').read_text())
def render(fmt,dim):
 w,h=dim['width'],dim['height'];folder=base/'media'/fmt
 paths=sorted(folder.glob('*.mp4'))
 cat=folder/'shots.txt';cat.write_text('\n'.join("file '"+str(p)+"'" for p in paths))
 inputs=['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(cat),'-i',str(base/'media/music.wav')]; filters=[];prev='0:v'
 for i,(a,b,txt,label) in enumerate(m['titles']):
  landscape=fmt=='16x9';cx=.19 if landscape else .5;cy=.55 if landscape else (.85 if fmt=='1x1' else .86);size=.031 if landscape else (.047 if fmt=='9x16' else .042)
  if landscape and a==0:txt='Build something\nyou’ll use\nevery day.'
  if landscape and a==24:txt='Color Kit Grande\nBuild. Explore.\nEnjoy.'
  im=Image.new('RGBA',(w,h));d=ImageDraw.Draw(im)
  for text,s,y,col in [(txt,size,cy,'white'),('ThingPulse',size*.48,.88 if landscape else .96,'#40d9c4'),(label,size*.40,.2 if landscape else .075,'#b8ccdb')]:
   font=ImageFont.truetype('/System/Library/Fonts/HelveticaNeue.ttc',round(s*w),index=1);box=d.multiline_textbbox((0,0),text,font=font,spacing=10,align='center'); d.multiline_text((cx*w-(box[2]-box[0])/2,(1-y)*h-(box[3]-box[1])/2-box[1]),text,font=font,fill=col,spacing=10,align='center')
  p=folder/f'title-{i}.png';im.save(p);inputs+=['-loop','1','-framerate','30','-i',str(p)];filters.append(f'[{prev}][{i+2}:v]overlay=enable=\'gte(t,{a})*lt(t,{b})\'[v{i}]');prev=f'v{i}'
 titlelist=folder/'titles.txt';titlelist.write_text(''.join("file '"+str(folder/f'title-{i}.png')+"'\nduration "+str(b-a)+"\n" for i,(a,b,_,_) in enumerate(m['titles']))+"file '"+str(folder/'title-7.png')+"'\n")
 titlemov=folder/'titles.mov'
 subprocess.run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(titlelist),'-vf','fps=30','-t','30','-c:v','qtrle','-threads','2',str(titlemov)],check=True)
 args=['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(cat),'-i',str(base/'media/music.wav'),'-i',str(titlemov),'-filter_complex_threads','1','-filter_complex','[0:v][2:v]overlay[v]','-map','[v]','-map','1:a','-t','30','-r','30','-c:v','libx264','-threads','2','-preset','fast','-crf','18','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-movflags','+faststart',str(base/f'ThingPulse-30s-{fmt}-draft.mp4')]
 subprocess.run(args,check=True);print(fmt,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:list(ex.map(lambda kv:render(*kv),m['formats'].items()))
