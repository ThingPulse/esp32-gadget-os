#!/usr/bin/env python3
"""Build reframed ad media and editable Resolve timelines; run generated import.lua in Resolve."""
from pathlib import Path
import subprocess, json, shutil, concurrent.futures
from PIL import Image
import opentimelineio as otio
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'documents/ads/color-kit-grande-30s'
SRC=Path('/Users/deichhor/Downloads/sources')
VID=ROOT/'documents/user-guide/video'
FORMATS={'16x9':(1920,1080,(730,50,1140,980)), '9x16':(1080,1920,(0,380,1080,1300)), '4x5':(1080,1350,(0,290,1080,910)), '1x1':(1080,1080,(0,250,1080,710))}
# start/end in the ad, source file, source in-point, shot category.
SHOTS=[(0,3,VID/'weather-tour-cozy.mp4',1.5,'app'),
 (3,4.5,SRC/'KitComponentsLandscape_GreyBackground.jpg',0,'kit'),
 (4.5,6,SRC/'Color Kit Grande Assembly Rendering.png',0,'still'),
 (6,7.5,next(SRC.glob('teenager-*.mov')),6,'stock'),
 (7.5,9,SRC/'CKGWorkshop.jpg',0,'still'),
 (9,13,VID/'weather-tour-cozy.mp4',9,'app'),
 (13,17,VID/'planes-tour-cozy.mp4',6,'app'),
 (17,21,VID/'stocks-tour-cozy.mp4',6.5,'app'),
 (21,22.5,next(SRC.glob('woman-*.mov')),1,'stock'),
 (22.5,24,VID/'enclosure-swipe-cozy.mp4',0,'app'),
 (24,30,SRC/'CKG_Rendering_WeatherApp.png',0,'hero')]
TITLES=[(0,3,'Build something\nyou’ll use every day.','GADGET OS'),(3,6,'Meet\nColor Kit Grande.','BUILD IT YOURSELF'),(6,9,'From parts\nto possibilities.','A KIT. YOUR CREATION.'),(9,13,'Your weather.\nAt a glance.','WEATHER'),(13,17,'Discover what’s\nflying overhead.','LIVE PLANES'),(17,21,'Keep your\nwatchlist close.','STOCKS'),(21,24,'Make it yours.','GADGET OS'),(24,30,'Color Kit Grande\nBuild. Explore. Enjoy.','Explore the kit → thingpulse.com')]
def run(a): subprocess.run(a,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
def media(fmt,idx,shot):
 w,h,(x,y,mw,mh)=FORMATS[fmt]; a,b,src,ss,kind=shot
 dest=OUT/'media'/fmt/f'{idx+1:02d}-{kind}.mp4';dest.parent.mkdir(parents=True,exist_ok=True)
 if dest.exists(): return dest
 if kind=='kit':
  src=SRC/({'9x16':'KitComponents9to16Vertical.jpg','4x5':'KitComponents4To5_GreyBackground.jpg','1x1':'KitComponents1To1_GreyBackground.jpg'}.get(fmt,'KitComponentsLandscape_GreyBackground.jpg'))
 still=src.suffix.lower() in ('.jpg','.png')
 inp=['-loop','1','-framerate','30'] if still else ['-ss',str(ss)]
 # The full device stays in frame; stock footage fills its picture area.
 crop='crop=900:1080:510:0,' if kind=='app' else ''
 fit='increase' if kind=='stock' else 'decrease'
 vf=f'{crop}scale={mw}:{mh}:force_original_aspect_ratio={fit}:force_divisible_by=2,'
 vf+=f'crop={mw}:{mh},' if kind=='stock' else f'pad={mw}:{mh}:(ow-iw)/2:(oh-ih)/2:color=0x101b29,'
 vf+=f'pad={w}:{h}:{x}:{y}:color=0x101b29,setsar=1,fps=30,format=yuv420p'
 run(['ffmpeg','-v','error','-y',*inp,'-i',str(src),'-t',str(b-a),'-vf',vf,'-an','-c:v','libx264','-preset','fast','-crf','18','-threads','2','-movflags','+faststart',str(dest)])
 return dest
OUT.mkdir(parents=True,exist_ok=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 jobs=[pool.submit(media,f,i,s) for f in FORMATS for i,s in enumerate(SHOTS)]
 for job in concurrent.futures.as_completed(jobs): print(job.result(),flush=True)
run(['ffmpeg','-v','error','-y','-i',str(SRC/'Dance loop 15.wav'),'-t','30','-af','afade=t=in:d=0.08,afade=t=out:st=28.8:d=1.2,volume=-2dB','-ar','48000',str(OUT/'media/music.wav')])
def rt(n): return otio.opentime.RationalTime(n,30)
def clip(path,n):
 c=otio.schema.Clip(name=path.stem,media_reference=otio.schema.ExternalReference(target_url=str(path),available_range=otio.opentime.TimeRange(rt(0),rt(n))),source_range=otio.opentime.TimeRange(rt(0),rt(n)));return c
for fmt,(w,h,_) in FORMATS.items():
 t=otio.schema.Timeline(name=f'ThingPulse - 30s - {fmt}',global_start_time=rt(108000))
 v=otio.schema.Track(name='Shots',kind=otio.schema.TrackKind.Video)
 for i,s in enumerate(SHOTS): v.append(clip(OUT/'media'/fmt/f'{i+1:02d}-{s[4]}.mp4',round((s[1]-s[0])*30)))
 a=otio.schema.Track(name='Music',kind=otio.schema.TrackKind.Audio);a.append(clip(OUT/'media/music.wav',900));t.tracks.extend([v,a]);otio.adapters.write_to_file(t,str(OUT/f'{fmt}.otio'))
 Image.new('RGBA',(w,h),(0,0,0,0)).save(OUT/'media'/fmt/'transparent.png')
 transparent=OUT/'media'/fmt/'transparent.mov'
 if not transparent.exists(): run(['ffmpeg','-v','error','-y','-loop','1','-framerate','30','-i',str(OUT/'media'/fmt/'transparent.png'),'-t','30','-c:v','qtrle','-threads','2',str(transparent)])
manifest={'formats':{k:{'width':v[0],'height':v[1]} for k,v in FORMATS.items()},'shots':[{'start':a,'end':b,'source':str(p),'source_in':s,'kind':k} for a,b,p,s,k in SHOTS],'titles':TITLES}
(OUT/'edit-manifest.json').write_text(json.dumps(manifest,indent=2))
def luaq(s): return json.dumps(s,ensure_ascii=False)
config=[]
for fmt,(w,h,_) in FORMATS.items():
 landscape=fmt=='16x9'
 # Headline position, with room above and below for branding.
 cx=.19 if landscape else .5
 cy=.55 if landscape else {'9x16':.86,'4x5':.86,'1x1':.85}[fmt]
 size=.031 if landscape else (.047 if fmt=='9x16' else .042)
 lines=[]
 for a,b,headline,label in TITLES:
  if landscape and a in (0,13,24):
   headline={0:'Build something\nyou’ll use\nevery day.',13:'Discover what’s\nflying overhead.',24:'Color Kit Grande\nBuild. Explore.\nEnjoy.'}[a]
  lines.append('{'+f'a={round(a*30)},b={round(b*30)},text={luaq(headline)},label={luaq(label)}'+'}')
 config.append('{'+f'name="{fmt}",w={w},h={h},cx={cx},cy={cy},size={size},brandY={.88 if landscape else .96},labelY={.2 if landscape else .075},titles={{'+','.join(lines)+'}}')
LUA='''local base = debug.getinfo(1,'S').source:sub(2):match('(.*/)')
local log=assert(io.open(base..'resolve-build.log','w'))
local function note(s) log:write(s..'\\n');log:flush();print(s) end
local function run()
local r=resolve or fusion:GetResolve()
local pm=r:GetProjectManager(); assert(pm:SaveProject())
local p=pm:CreateProject('ThingPulse - Color Kit Grande - 30s v3') or pm:LoadProject('ThingPulse - Color Kit Grande - 30s v3')
assert(p and p:GetTimelineCount()==0, 'Use a fresh project name for regeneration')
p:SetSetting('timelineFrameRate','30')
local mp=p:GetMediaPool()
local formats=CONFIG
for _,f in ipairs(formats) do
 p:SetSetting('timelineResolutionWidth',tostring(f.w));p:SetSetting('timelineResolutionHeight',tostring(f.h))
 local paths={}
 for _,name in ipairs({'01-app.mp4','02-kit.mp4','03-still.mp4','04-stock.mp4','05-still.mp4','06-app.mp4','07-app.mp4','08-app.mp4','09-stock.mp4','10-app.mp4','11-hero.mp4'}) do table.insert(paths,base..'media/'..f.name..'/'..name) end
 table.insert(paths,base..'media/music.wav');local imported=assert(mp:ImportMedia(paths));local byName={};for _,m in ipairs(imported) do byName[m:GetName()]=m end;local media={};for _,path in ipairs(paths) do table.insert(media,assert(byName[path:match('([^/]+)$')])) end
 local t=assert(mp:CreateEmptyTimeline('ThingPulse - 30s - '..f.name))
 p:SetCurrentTimeline(t)
 local durations={90,45,45,45,45,120,120,120,45,45,180}; local at=t:GetStartFrame()
 for i,n in ipairs(durations) do assert(mp:AppendToTimeline({{mediaPoolItem=media[i],startFrame=0,endFrame=n,recordFrame=at,mediaType=1,trackIndex=1}}));at=at+n end
 assert(mp:AppendToTimeline({{mediaPoolItem=media[12],startFrame=0,endFrame=900,recordFrame=t:GetStartFrame(),mediaType=2,trackIndex=1}}))
 p:SetCurrentTimeline(t)
 t:SetSetting('useCustomSettings','1');t:SetSetting('timelineResolutionWidth',tostring(f.w));t:SetSetting('timelineResolutionHeight',tostring(f.h))
 assert(t:GetEndFrame()-t:GetStartFrame()==900)
 t:AddTrack('video')
 local placeholder=assert(mp:ImportMedia({base..'media/'..f.name..'/transparent.mov'})[1])
 for _,s in ipairs(f.titles) do
  local item=assert(mp:AppendToTimeline({{mediaPoolItem=placeholder,startFrame=0,endFrame=s.b-s.a,recordFrame=t:GetStartFrame()+s.a,mediaType=1,trackIndex=2}})[1])
  assert(item:GetDuration()==s.b-s.a, 'Title duration mismatch')
  local comp=assert(item:AddFusionComp())
  local function text(name,value,size,x,y,color)
   local tx=comp:AddTool('TextPlus');tx:SetAttrs({TOOLS_Name=name});tx:SetInput('StyledText',value);tx:SetInput('Font','Helvetica Neue');tx:SetInput('Style','Bold');tx:SetInput('Size',size);tx:SetInput('Center',{x,y});tx:SetInput('Red1',color[1]);tx:SetInput('Green1',color[2]);tx:SetInput('Blue1',color[3]);return tx
  end
  local headline=text('Headline',s.text,f.size,f.cx,f.cy,{1,1,1})
  local brand=text('Brand','ThingPulse',f.size*.48,f.cx,f.brandY,{.25,.85,.77})
  local label=text('Caption',s.label,f.size*.40,f.cx,f.labelY,{.72,.8,.86})
  local m1=comp:AddTool('Merge');m1:ConnectInput('Background',headline);m1:ConnectInput('Foreground',brand)
  local m2=comp:AddTool('Merge');m2:ConnectInput('Background',m1);m2:ConnectInput('Foreground',label)
  local output=comp:FindTool('MediaOut1') or comp:AddTool('MediaOut');output:ConnectInput('Input',m2)
 end
 assert(t:GetEndFrame()-t:GetStartFrame()==900)
 assert(#t:GetItemListInTrack('video',1)==11);assert(#t:GetItemListInTrack('video',2)==8)
 t:SetCurrentTimecode('01:00:01:00')
 assert(t:Export(base..'ThingPulse-30s-'..f.name..'.drt',r.EXPORT_DRT))
 p:SetCurrentRenderMode(1);assert(p:SetCurrentRenderFormatAndCodec('mp4','H264'))
 assert(p:SetRenderSettings({SelectAllFrames=true,TargetDir=base,CustomName='ThingPulse-30s-'..f.name,ExportVideo=true,ExportAudio=true,FormatWidth=f.w,FormatHeight=f.h,FrameRate=30,AudioCodec='aac'}))
 assert(p:AddRenderJob());note('Prepared '..f.name)
end
assert(pm:SaveProject());assert(pm:ExportProject(p:GetName(),base..'ThingPulse-30s.drp'))
assert(p:StartRendering());note('All four timelines exported; rendering previews')
end
local ok,err=xpcall(run,debug.traceback);if not ok then note('FAILED: '..tostring(err)) end
log:close()
'''.replace('CONFIG','{'+','.join(config)+'}')
(OUT/'import.lua').write_text(LUA)
print('READY',flush=True)
