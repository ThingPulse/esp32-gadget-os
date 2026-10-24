local base = debug.getinfo(1,'S').source:sub(2):match('(.*/)')
local log=assert(io.open(base..'resolve-build.log','w'))
local function note(s) log:write(s..'\n');log:flush();print(s) end
local function run()
local r=resolve or fusion:GetResolve()
local pm=r:GetProjectManager(); assert(pm:SaveProject())
local p=pm:CreateProject('ThingPulse - Color Kit Grande - 30s v3') or pm:LoadProject('ThingPulse - Color Kit Grande - 30s v3')
assert(p and p:GetTimelineCount()==0, 'Use a fresh project name for regeneration')
p:SetSetting('timelineFrameRate','30')
local mp=p:GetMediaPool()
local formats={{name="16x9",w=1920,h=1080,cx=0.19,cy=0.55,size=0.031,brandY=0.88,labelY=0.2,titles={{a=0,b=90,text="Build something\nyou’ll use\nevery day.",label="GADGET OS"},{a=90,b=180,text="Meet\nColor Kit Grande.",label="BUILD IT YOURSELF"},{a=180,b=270,text="From parts\nto possibilities.",label="A KIT. YOUR CREATION."},{a=270,b=390,text="Your weather.\nAt a glance.",label="WEATHER"},{a=390,b=510,text="Discover what’s\nflying overhead.",label="LIVE PLANES"},{a=510,b=630,text="Keep your\nwatchlist close.",label="STOCKS"},{a=630,b=720,text="Make it yours.",label="GADGET OS"},{a=720,b=900,text="Color Kit Grande\nBuild. Explore.\nEnjoy.",label="Explore the kit → thingpulse.com"}}},{name="9x16",w=1080,h=1920,cx=0.5,cy=0.86,size=0.047,brandY=0.96,labelY=0.075,titles={{a=0,b=90,text="Build something\nyou’ll use every day.",label="GADGET OS"},{a=90,b=180,text="Meet\nColor Kit Grande.",label="BUILD IT YOURSELF"},{a=180,b=270,text="From parts\nto possibilities.",label="A KIT. YOUR CREATION."},{a=270,b=390,text="Your weather.\nAt a glance.",label="WEATHER"},{a=390,b=510,text="Discover what’s\nflying overhead.",label="LIVE PLANES"},{a=510,b=630,text="Keep your\nwatchlist close.",label="STOCKS"},{a=630,b=720,text="Make it yours.",label="GADGET OS"},{a=720,b=900,text="Color Kit Grande\nBuild. Explore. Enjoy.",label="Explore the kit → thingpulse.com"}}},{name="4x5",w=1080,h=1350,cx=0.5,cy=0.86,size=0.042,brandY=0.96,labelY=0.075,titles={{a=0,b=90,text="Build something\nyou’ll use every day.",label="GADGET OS"},{a=90,b=180,text="Meet\nColor Kit Grande.",label="BUILD IT YOURSELF"},{a=180,b=270,text="From parts\nto possibilities.",label="A KIT. YOUR CREATION."},{a=270,b=390,text="Your weather.\nAt a glance.",label="WEATHER"},{a=390,b=510,text="Discover what’s\nflying overhead.",label="LIVE PLANES"},{a=510,b=630,text="Keep your\nwatchlist close.",label="STOCKS"},{a=630,b=720,text="Make it yours.",label="GADGET OS"},{a=720,b=900,text="Color Kit Grande\nBuild. Explore. Enjoy.",label="Explore the kit → thingpulse.com"}}},{name="1x1",w=1080,h=1080,cx=0.5,cy=0.85,size=0.042,brandY=0.96,labelY=0.075,titles={{a=0,b=90,text="Build something\nyou’ll use every day.",label="GADGET OS"},{a=90,b=180,text="Meet\nColor Kit Grande.",label="BUILD IT YOURSELF"},{a=180,b=270,text="From parts\nto possibilities.",label="A KIT. YOUR CREATION."},{a=270,b=390,text="Your weather.\nAt a glance.",label="WEATHER"},{a=390,b=510,text="Discover what’s\nflying overhead.",label="LIVE PLANES"},{a=510,b=630,text="Keep your\nwatchlist close.",label="STOCKS"},{a=630,b=720,text="Make it yours.",label="GADGET OS"},{a=720,b=900,text="Color Kit Grande\nBuild. Explore. Enjoy.",label="Explore the kit → thingpulse.com"}}}}
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
